#!/usr/bin/env python
#
# Copyright 2018 - The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the',  help='License');
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an',  help='AS IS' BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import json
import os
import requests
import sys
import urllib

from absl import app
from absl import flags
from absl import logging

FLAGS = flags.FLAGS
flags.DEFINE_string('environment', 'prod',
                    'Which symbol server endpoint to use, can be staging or prod')
flags.DEFINE_string(
    'api_key', None, 'Which api key to use. By default will load contents of ~/.emulator_symbol_server_key')
flags.DEFINE_string('symbol_file', None, 'The symbol file you wish to process')
flags.DEFINE_boolean(
    "force", False, "Upload symbol file, even though it is already available.")

flags.register_validator('environment', lambda value: value.lower() == 'prod' or value.lower() ==
                         'staging', message='--environment should be either prod or staging (case insensitive).')
flags.mark_flag_as_required('symbol_file')


class SymbolFileServer(object):
    """Class to verify and upload symbols to the crash symbol api v1."""

    # The api key should work for both staging and production.
    API_KEY_FILE = os.path.join(os.path.expanduser(
        '~'), '.emulator_symbol_server_key')
    API_URL = {
        'prod': 'https://prod-crashsymbolcollector-pa.googleapis.com/v1',
        'staging': 'https://staging-crashsymbolcollector-pa.googleapis.com/v1'
    }

    STATUS_MSG = {
        'OK': 'The symbol data was written to symbol storage.',
        'DUPLICATE_DATA': 'The symbol data was identical to data already in storage, so no changes were made.'
    }

    # Timeout for individual web requests.  an exception is raised if the server has not issued a response for timeout
    # seconds (more precisely, if no bytes have been received on the underlying socket for timeout seconds).
    DEFAULT_TIMEOUT = 30

    def __init__(self, environment='prod', api_key=None):
        self.api_url = SymbolFileServer.API_URL[environment.lower()]
        if not api_key:
            with open(SymbolFileServer.API_KEY_FILE) as f:
                self.api_key = f.read()
        else:
            self.api_key = api_key

    def _extract_symbol_info(self, symbol_file):
        '''Extracts os, archictecture, debug id and debug file.

        Raises an error if the first line of the file does not contain:
        MODULE {os} {arch} {id} {filename}
        '''
        with open(symbol_file, 'r') as f:
            info = f.readline().split()

        if len(info) != 5 or info[0] != 'MODULE':
            raise('Corrupt symbol file: %s, %s' % (symbol_file, info))

        _, os, arch, dbg_id, dbg_file = info
        return os, arch, dbg_id, dbg_file

    def _exec_request(self, oper, url, **kwargs):
        '''Makes a web request with default timeout, returning the json result.

           The api key will be added to the url request as a parameter.

           This method will raise a requests.exceptions.HTTPError
           if the status code is not 4XX, 5XX

           Note: If you are using verbose logging it is entirely possible that the subsystem will 
           write your api key to the logs!
        '''
        resp = requests.request(
            oper, url, params={'key': self.api_key}, timeout=SymbolFileServer.DEFAULT_TIMEOUT, **kwargs)

        if resp.status_code > 399:
            # Make sure we don't leak secret keys by accident.
            resp.url = resp.url.replace(urllib.quote(self.api_key), 'XX-HIDDEN-XX')
            logging.error('Url: %s, Status: %s, response: "%s", in: %s',
                          resp.url, resp.status_code, resp.text, resp.elapsed)
            resp.raise_for_status()

        if resp.content:
            return resp.json()
        return {}

    def is_available(self, symbol_file):
        """True if the symbol_file is available on the server."""
        _, _, dbg_id, dbg_file = self._extract_symbol_info(symbol_file)
        url = '{}/symbols/{}/{}:checkStatus'.format(
            self.api_url, dbg_file, dbg_id)
        status = self._exec_request('get', url)
        return status['status'] == 'FOUND'

    def upload(self, symbol_file):
        """Makes the symbol_file available on the server, returning the status result."""
        _, _, dbg_id, dbg_file = self._extract_symbol_info(symbol_file)
        upload = self._exec_request('post',
                                    '{}/uploads:create'.format(self.api_url))
        self._exec_request('put',
                           upload['uploadUrl'],
                           data=open(symbol_file, 'r'))
        status = self._exec_request('post',
                                    '{}/uploads/{}:complete'.format(
                                        self.api_url, upload['uploadKey']),
                                    data=json.dumps(
                                        {'symbol_id': {'debug_file': dbg_file, 'debug_id': dbg_id}}))

        return status['result']


def main(args):
    # The lower level enging will log individual requests!
    logging.debug("---------------------------------------------------------------------")
    logging.debug("--- WARNING!! You are likely going to leak your api key WARNING!! ---")
    logging.debug("---------------------------------------------------------------------")

    server = SymbolFileServer(FLAGS.environment, FLAGS.api_key)
    if FLAGS.force or not server.is_available(FLAGS.symbol_file):
        status = server.upload(FLAGS.symbol_file)
        print(SymbolFileServer.STATUS_MSG.get(
            status, 'Undefined or unknown result {}'.format(status)))
    else:
        print("Symbol already available, skipping upload.")


def launch():
    app.run(main)


if __name__ == '__main__':
    launch()
