// Copyright 2018 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "android-qemu2-glue/drive-share.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "android/base/files/FileShareOpen.h"
#include "android/base/files/PathUtils.h"
#include "android/base/memory/LazyInstance.h"
#include "android/globals.h"
#include "android/multi-instance.h"
#include "android/utils/bufprint.h"
#include "android/utils/eintr_wrapper.h"
#include "android/utils/path.h"
#include "android/utils/tempfile.h"

extern "C" {
#include "block/block.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "qemu/config-file.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/option_int.h"
#include "qemu/osdep.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "sysemu/sysemu.h"
}

namespace {
struct DriveShare {
    std::vector<DriveInfo*> driveInfoList = {};
    std::vector<QemuOpts*> qemuOptsList = {};
    std::unordered_map<std::string, std::string> srcImagePaths = {};
    BlockInterfaceType blockDefaultType;
};
static android::base::LazyInstance<DriveShare> sDriveShare = LAZY_INSTANCE_INIT;
struct DriveInitParam {
    BlockInterfaceType blockDefaultType;
    const char* snapshotName;
    android::base::FileShare shareMode;
    bool baseNeedApplySnapshot;
};
}  // namespace

#define QCOW2_SUFFIX "qcow2"

typedef struct {
    const char* drive;
    const char* backing_image_path;
} DriveBackingPair;

static bool read_file_to_buf(const char* file, uint8_t* buf, size_t size) {
    int fd = HANDLE_EINTR(open(file, O_RDONLY | O_BINARY));
    if (fd < 0) {
        return false;
    }
    ssize_t ret = HANDLE_EINTR(read(fd, buf, size));
    IGNORE_EINTR(close(fd));
    if (ret != size) {
        return false;
    }
    return true;
}

static bool parseQemuOptForQcow2(bool wipeData) {
    /* First, determine if any of the backing images have been altered.
     * QCoW2 images won't work in that case, and need to be recreated (this
     * will obliterate previous snapshots).
     * The most reliable way to do this is to cache some sort of checksum of
     * the image files, but we go the easier (and faster) route and cache the
     * version number that is specified in build.prop.
     */
    const char* avd_data_dir = avdInfo_getContentPath(android_avdInfo);
    static const char sysimg_version_number_cache_basename[] =
            "version_num.cache";
    char* sysimg_version_number_cache_path =
            path_join(avd_data_dir, sysimg_version_number_cache_basename);
    bool reset_version_number_cache = false;
    if (!path_exists(sysimg_version_number_cache_path)) {
        /* File with previously saved version number doesn't exist,
         * we'll create it later.
         */
        reset_version_number_cache = true;
    } else {
        FILE* vn_cache_file = fopen(sysimg_version_number_cache_path, "r");
        int sysimg_version_number = -1;
        /* If the file with version number contained an error, or the
         * saved version number doesn't match the current one, we'll
         * update it later.
         */
        reset_version_number_cache =
                vn_cache_file == NULL ||
                fscanf(vn_cache_file, "%d", &sysimg_version_number) != 1 ||
                sysimg_version_number !=
                        avdInfo_getSysImgIncrementalVersion(android_avdInfo);
        if (vn_cache_file) {
            fclose(vn_cache_file);
        }
    }

    /* List of paths to all images that can be mounted.*/
    const DriveBackingPair image_paths[] = {
            {"system", android_hw->disk_systemPartition_path
                               ?: android_hw->disk_systemPartition_initPath},
            {"vendor", android_hw->disk_vendorPartition_path
                               ?: android_hw->disk_vendorPartition_initPath},
            {"cache", android_hw->disk_cachePartition_path},
            {"userdata", android_hw->disk_dataPartition_path},
            {"sdcard", android_hw->hw_sdCard_path},
            {"encrypt", android_hw->disk_encryptionKeyPartition_path},
    };
    /* List of paths to all images for cros.*/
    const DriveBackingPair image_paths_hw_arc[] = {
            {"system", android_hw->disk_systemPartition_initPath},
            {"vendor", android_hw->disk_vendorPartition_initPath},
            {"userdata", android_hw->disk_dataPartition_path},
    };
    int count = sizeof(image_paths) / sizeof(image_paths[0]);
    const DriveBackingPair* images = image_paths;
    if (android_hw->hw_arc) {
        count = sizeof(image_paths_hw_arc) / sizeof(image_paths_hw_arc[0]);
        images = image_paths_hw_arc;
    }
    int p;
    QemuOptsList* optList = qemu_find_opts("drive");
    for (p = 0; p < count; p++) {
        QemuOpts* opts = qemu_opts_find(optList, images[p].drive);
        if (opts) {
            sDriveShare->srcImagePaths.emplace(images[p].drive,
                                               qemu_opt_get(opts, "file"));
        }
        const char* backing_image_path = images[p].backing_image_path;
        if (!backing_image_path || *backing_image_path == '\0') {
            /* If the path is NULL or empty, just ignore it.*/
            continue;
        }
        char* image_basename = path_basename(backing_image_path);
        char* qcow2_path_buffer = NULL;
        const char* qcow2_image_path = NULL;
        bool need_create_tmp = false;
        // ChromeOS and Android pass parameters differently
        if (android_hw->hw_arc) {
            if (p < 2) {
                /* System & vendor image are special cases, the backing image is
                 * in the SDK folder, but the QCoW2 image that the emulator
                 * uses is created on a per-AVD basis and is placed in the
                 * AVD's data folder.
                 */
                const char qcow2_suffix[] = "." QCOW2_SUFFIX;
                size_t path_size =
                        strlen(image_basename) + sizeof(qcow2_suffix) + 1;
                char* image_qcow2_basename = (char*)malloc(path_size);
                bufprint(image_qcow2_basename, image_qcow2_basename + path_size,
                         "%s%s", image_basename, qcow2_suffix);
                qcow2_path_buffer =
                        path_join(avd_data_dir, image_qcow2_basename);
                free(image_qcow2_basename);
            } else {
                /* For all the other images except system image,
                 * just create another file alongside them
                 * with a 'qcow2' extension
                 */
                const char qcow2_suffix[] = "." QCOW2_SUFFIX;
                size_t path_size =
                        strlen(backing_image_path) + sizeof(qcow2_suffix) + 1;
                qcow2_path_buffer = (char*)malloc(path_size);
                bufprint(qcow2_path_buffer, qcow2_path_buffer + path_size,
                         "%s%s", backing_image_path, qcow2_suffix);
            }
            qcow2_image_path = qcow2_path_buffer;
            sDriveShare->srcImagePaths[images[p].drive] = qcow2_image_path;
        } else {
            qcow2_image_path = qemu_opt_get(opts, "file");
            const char qcow2_suffix[] = "." QCOW2_SUFFIX;
            if (strcmp(qcow2_suffix, qcow2_image_path +
                                             strlen(qcow2_image_path) -
                                             strlen(qcow2_suffix))) {
                // We are not using qcow2
                continue;
            }
        }

        Error* img_creation_error = NULL;
        if (!path_exists(qcow2_image_path) || wipeData ||
            reset_version_number_cache) {
            const char* fmt = "raw";
            uint8_t buf[BLOCK_PROBE_BUF_SIZE];
            BlockDriver* drv;
            drv = bdrv_find_format("qcow2");
            if (drv && read_file_to_buf(backing_image_path, buf, sizeof(buf)) &&
                drv->bdrv_probe(buf, sizeof(buf), backing_image_path) >= 100) {
                fmt = "qcow2";
            }
            bdrv_img_create(qcow2_image_path, QCOW2_SUFFIX,
                            /*absolute path only for sys vendor*/
                            p < 2 ? backing_image_path : image_basename, fmt,
                            NULL, -1, 0, true, &img_creation_error);
        }
        free(image_basename);
        free(qcow2_path_buffer);
        if (img_creation_error) {
            error_report("%s", error_get_pretty(img_creation_error));
            return false;
        }
    }

    /* Update version number cache if necessary. */
    if (reset_version_number_cache) {
        FILE* vn_cache_file = fopen(sysimg_version_number_cache_path, "w");
        if (vn_cache_file) {
            fprintf(vn_cache_file, "%d\n",
                    avdInfo_getSysImgIncrementalVersion(android_avdInfo));
            fclose(vn_cache_file);
        }
    }
    free(sysimg_version_number_cache_path);

    return true;
}

static bool needRemount(const char* id) {
    return strcmp("system", id) && strcmp("vendor", id);
}

static bool needCreateTmp(const char* id, android::base::FileShare shareMode) {
    return shareMode == android::base::FileShare::Read && needRemount(id) &&
           android::base::PathUtils::extension(
                   sDriveShare->srcImagePaths[id]) == ".qcow2";
}

static bool createEmptySnapshot(BlockDriverState* bs,
                                const char* snapshotName) {
    QEMUSnapshotInfo sn;
    memset(&sn, 0, sizeof(sn));
    pstrcpy(sn.name, sizeof(sn.name), snapshotName);

    qemu_timeval tv;
    qemu_gettimeofday(&tv);
    sn.date_sec = tv.tv_sec;
    sn.date_nsec = tv.tv_usec * 1000;

    // bdrv_snapshot_create returns 0 on success
    return 0 == bdrv_snapshot_create(bs, &sn);
}

static std::string initDrivePath(const char* id,
                                 android::base::FileShare shareMode) {
    assert(sDriveShare->srcImagePaths.count(id));
    if (needCreateTmp(id, shareMode)) {
        // Create a temp qcow2-on-qcow2
        Error* img_creation_error = NULL;
        TempFile* img = tempfile_create_with_ext(".qcow2");
        const char* imgPath = tempfile_path(img);
        bdrv_img_create(imgPath, QCOW2_SUFFIX,
                        sDriveShare->srcImagePaths[id].c_str(), "qcow2",
                        nullptr, -1, 0, true, &img_creation_error);
        if (img_creation_error) {
            tempfile_close(img);
            error_report("%s", error_get_pretty(img_creation_error));
            return "";
        }
        return imgPath;
    } else {
        return sDriveShare->srcImagePaths[id];
    }
}

// This is for C-style function pointer
extern "C" {
static int drive_init(void* opaque, QemuOpts* opts, Error** errp) {
    DriveInitParam* param = (DriveInitParam*)opaque;
    const char* id = opts->id;
    if (id) {
        std::string path = initDrivePath(id, param->shareMode);
        qemu_opt_set(opts, "file", path.c_str(), errp);
        if (needCreateTmp(id, param->shareMode) && param->snapshotName) {
            if (strcmp(id, "cache")) {
                int res = 0;
                if (param->baseNeedApplySnapshot) {
                    // handle the snapshot
                    BlockBackend* blk =
                            blk_new_open(sDriveShare->srcImagePaths[id].c_str(), NULL,
                                         qdict_new(), BDRV_O_RDWR, nullptr);
                    assert(blk);
                    BlockDriverState* bs = blk_bs(blk);
                    assert(bs);
                    // bdrv_snapshot_goto can fail during first boot or wipe data
                    res = bdrv_snapshot_goto(bs, param->snapshotName, nullptr);
                    blk_flush(blk);
                    blk_unref(blk);
                }
                if (res == 0) {
                    // Create an empty snapshot in the qcow2-on-qcow2
                    BlockBackend* blkNew = blk_new_open(
                            path.c_str(), NULL, qdict_new(), BDRV_O_RDWR, nullptr);
                    assert(blkNew);
                    createEmptySnapshot(blk_bs(blkNew), param->snapshotName);
                    blk_unref(blkNew);
                }
            } else {
                // Cache image doesn't work well with bdrv_snapshot_create.
                // It complaints when loading a snapshot.
                // Thus we directly copy the qcow2 file.
                // TODO (yahan@): figure out why
                path_copy_file(path.c_str(),
                        sDriveShare->srcImagePaths[id].c_str());
                QDict *options = qdict_new();
                qdict_put(options, "driver", qstring_from_str(QCOW2_SUFFIX));
                Error *local_err = NULL;
                BlockBackend *blk = blk_new_open(path.c_str(),
                        NULL, options, BDRV_O_RDWR | BDRV_O_NO_BACKING,
                        &local_err);
                if (!blk) {
                    error_report("Could not open '%s': ", path.c_str());
                } else {
                    BlockDriverState* bs = blk_bs(blk);
                    bdrv_change_backing_file(bs,
                            android_hw->disk_cachePartition_path, NULL);
                    blk_unref(blk);
                }
            }
        }
    }
    DriveInfo* driveInfo = drive_new(opts, param->blockDefaultType);
    return nullptr == driveInfo;
}

static int drive_reinit(void* opaque, QemuOpts* opts, Error** errp) {
    const char* id = opts->id;
    if (!needRemount(id)) {
        return 0;
    }
    DriveInitParam* param = (DriveInitParam*)opaque;
    const char* snapshotName = param->snapshotName;
    BlockBackend* blk = blk_by_name(id);
    if (!blk) {
        error_setg(errp, "%s not found", id);
        return 1;
    }

    BlockDriverState* oldbs = blk_bs(blk);
    AioContext* aioCtx = bdrv_get_aio_context(oldbs);
    aio_context_acquire(aioCtx);
    if (needCreateTmp(id, param->shareMode)) {
        bdrv_flush(oldbs);
        // Set the base file to use the snapshot
        int res = bdrv_snapshot_goto(oldbs, snapshotName, errp);
        if (res) {
            error_setg(errp, "bdrv_snapshot_goto failed %s <- %s", id,
                       snapshotName);
            aio_context_release(aioCtx);
            return 1;
        }
    }
    blk_remove_bs(blk);
    aio_context_release(aioCtx);

    std::string path = initDrivePath(id, param->shareMode);
    qemu_opt_set(opts, "file", path.c_str(), errp);

    // Setup qemu opts for the drive.
    // For opts, please refer to drive_new() and blockdev_init() for what needs
    // to be set.
    // (drive_new also sets dinfo and error handlings. We will not touch dinfo
    // and we don't set error handlings. We also skip the renaming in drive_new
    // because it should have been renamed in the first time of initialization.)
    QDict* bs_opts = qdict_new();
    qemu_opts_to_qdict(opts, bs_opts);
    const char* file = qemu_opt_get(opts, "file");
    QemuOpts* legacy_opts =
            qemu_opts_create(&qemu_legacy_drive_opts, NULL, 0, nullptr);
    qemu_opts_absorb_qdict(legacy_opts, bs_opts, errp);
    QemuOpts* drive_opts = qemu_opts_find(&qemu_common_drive_opts, id);
    if (drive_opts) {
        qemu_opts_del(drive_opts);
        drive_opts = nullptr;
    }
    drive_opts = qemu_opts_create(&qemu_common_drive_opts, id, 1, nullptr);
    qemu_opts_absorb_qdict(drive_opts, bs_opts, errp);

    bool read_only = qemu_opt_get_bool(legacy_opts, BDRV_OPT_READ_ONLY, false);
    qdict_set_default_str(bs_opts, BDRV_OPT_CACHE_DIRECT, "off");
    qdict_set_default_str(bs_opts, BDRV_OPT_CACHE_NO_FLUSH, "off");
    qdict_set_default_str(bs_opts, BDRV_OPT_READ_ONLY,
                          read_only ? "on" : "off");
    qdict_del(bs_opts, "id");

    BlockDriverState* bs = bdrv_open(file, nullptr, bs_opts, 0, errp);
    if (!bs) {
        error_setg(errp, "drive %s open failure", file);
        return 1;
    }

    if (needCreateTmp(id, param->shareMode)) {
        // fake an empty snapshot
        // We don't worry if it fails.
        createEmptySnapshot(bs, param->snapshotName);
    }

    int res = blk_insert_bs(blk, bs, errp);
    bdrv_unref(bs);
    return res;
}
}

static std::string getReadSnapshotFileName() {
    char* avdPath = path_getAvdContentPath(android_hw->avd_name);
    std::string baseSnapshotFileName(avdPath);
    free(avdPath);
    return baseSnapshotFileName + PATH_SEP "read-snapshot.txt";
}

static bool isBaseOnDifferentSnapshot(const char* snapshot_name) {
    std::string baseSnapshotFileName = getReadSnapshotFileName();
    FILE* baseSnapshotNameFile = fsopenWithTimeout(baseSnapshotFileName.c_str(),
            "r", android::base::FileShare::Read, 5000);
    if (!baseSnapshotNameFile) {
        return true;
    }
    // We need to compare if snapshot_name is the same as the name in
    // baseSnapshotNameFile.
    // To do so, we don't need to read the full name in baseSnapshotFileName.
    // We only need to read the portion that is slightly larger than
    // snapshot_name. (If it is larger then they are not the same.)
    const int bufferSize = strlen(snapshot_name) + 2;
    std::unique_ptr<char[]> buffer(new char[bufferSize]);
    fgets(buffer.get(), bufferSize, baseSnapshotNameFile);
    fclose(baseSnapshotNameFile);
    return strcmp(snapshot_name, buffer.get());
}

static bool updateDriveShareMode(const char* snapshotName,
                                 android::base::FileShare shareMode) {
    DriveInitParam param = {sDriveShare->blockDefaultType, snapshotName,
                            shareMode};
    Error* error = NULL;
    int res = qemu_opts_foreach(qemu_find_opts("drive"), drive_reinit, &param,
                                &error);
    if (res) {
        error_report("%s", error_get_pretty(error));
        return false;
    }
    return true;
}

extern "C" int android_drive_share_init(bool wipe_data,
                                        bool read_only,
                                        const char* snapshot_name,
                                        BlockInterfaceType blockDefaultType) {
    if (!parseQemuOptForQcow2(wipe_data)) {
        return -1;
    }

    android::multiinstance::setUpdateDriveShareModeFunc(updateDriveShareMode);
    bool needApplySnapshot = read_only && isBaseOnDifferentSnapshot(snapshot_name);
    DriveInitParam param = {blockDefaultType, snapshot_name,
                            read_only ? android::base::FileShare::Read
                                      : android::base::FileShare::Write,
                            needApplySnapshot};
    FILE* baseSnapshotNameFile = nullptr;
    if (needApplySnapshot || !read_only) {
        // For read-only we open a file to record the snapshot name
        // For writable we wipe that snapshot name because it might change later
        std::string baseSnapshotFileName = getReadSnapshotFileName();
        baseSnapshotNameFile = fsopenWithTimeout(baseSnapshotFileName.c_str(),
            "w", android::base::FileShare::Write, 5000);
    }
    sDriveShare->blockDefaultType = blockDefaultType;
    if (qemu_opts_foreach(qemu_find_opts("drive"), drive_init, &param, NULL)) {
        if (baseSnapshotNameFile) {
            fclose(baseSnapshotNameFile);
        }
        return -1;
    }
    if (baseSnapshotNameFile) {
        if (read_only && snapshot_name) {
            fprintf(baseSnapshotNameFile, "%s", snapshot_name);
        }
        fclose(baseSnapshotNameFile);
    }
    return 0;
}
