// Copyright (C) 2017 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "android/base/Compiler.h"
#include "android/base/memory/LazyInstance.h"
#include "android/base/TypeTraits.h"

#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

//
// OnDemand<T> - a wrapper class for on-demand initialization of variables.
//
// Sometimes a function or a class contains some variables that are only used
// if some condition is met, e.g.:
//
//      void boo(bool print) {
//          Printer printer;
//          ...
//          if (print) printer.out("data");
//      }
//
// It's usually OK to have code like this, but if |printer| initialization is
// slow, every call with |print| == false would waste quite noticeable amount of
// time for nothing while creating |printer| to never use it.
//
// OnDemand<T> solves the problem - its initialization only captures the
// parameters needed to construct T, but T itself is only constructed on the
// first use:
//
//      void boo(bool print) {
//          auto printer = makeOnDemand<Printer>();
//          ...
//          if (print) printer->out("data");
//      }
//
// This new version of code would only call Printer's ctor if |print| is true
// and we needed to call some |printer|'s method.
//
// This is especially useful for class members, where one sometimes there's
// a part like that, but tracking its initialized state across multiple member
// functions is hard and error-prone. OnDemand<T> hides it from the user.
//
// Usage:
//
//  // Initialize |i| to 10 on first use:
//  auto i = makeOnDemand<int>(10);
//  // Initialize |i| to 10000th prime on first use:
//  auto i = makeOnDemand<int>([] { return std::make_tuple(calc10000thPrime()); });
//  // Create a complex object with tons of parameters:
//  auto obj = makeOnDemand<QWindow>([app, text] {
//          return std::make_tuple(app, translate(text));
//      });
//  // Explicitly destroy the object:
//  obj.clear();
//
//  // Add an on-demand class member:
//  class C {
//      MemberOnDemand<QWindow, QApplication, string> mWindow;
//      C() : mWindow([app, text] { return std::make_tuple(...); }) {}
//  };
//

namespace android {
namespace base {

namespace internal {

//
// A bit of template magic to unpack arguments from a tuple and call a function
// using those arguments:
//    std::tuple<int, char, char*> -> func(tuple<0>, tuple<1>, tuple<2>)
//
// This can be done with a simple set of transformations:
// 1. Generate a class to incapsulate integer sequence 0, 1, ..., <num_args>
//      Seq<int...> is this class
//
// 2. Create a 'maker' class to construct Seq<int...> given only <num_args>
//    value.
//      MakeSeq<N, S...> works this way, e.g.
//
//      MakeSeq<2> inherits MakeSeq<2 - 1, 2 - 1> == MakeSeq<1, 1>
//          MakeSeq<1, 1> : MakeSeq<1 - 1, 1 - 1, 1> == MakeSeq<0, 0, 1>
//          MakeSeq<0, 0, 1> == MakeSeq<0, S...> and defines |type| = Seq<0, 1>
//
// 3. Later one can use MakeSeqT alias to quickly create Seq<...>:
//      MakeSeqT<3> == Seq<0, 1, 2>
//
// 4. To call a function with arguments from a tuple one needs to use syntax:
//      template <class ... Args>
//      void call(std::tuple<Args> args) {
//          call_impl(args, MakeSeqT<sizeof...(Args)>());
//      }
//      template <class ... Args, int ... N>
//      void call_impl(std::tuple<Args> args, Seq<N...>) {
//          func(std::get<N>(args)...); // expands std::get<>() for each N
//      }
//
//  TODO: C++14 gives us std::index_sequence<> in STL, so we'll be able to get
//        rid of ##1-3.
//        C++17 got std::apply() call to replace all of this altogether.
//

template <int...>
struct Seq {};

template <int N, int... S>
struct MakeSeq : MakeSeq<N - 1, N - 1, S...> {};

template <int... S>
struct MakeSeq<0, S...> {
    using type = Seq<S...>;
};

template <int... S>
using MakeSeqT = typename MakeSeq<S...>::type;

// If user wants to create an OnDemand<> wrapper with already calculated
// parameters, this class would hold those and convert them into a tuple
// to call constructor.
template <class... Args>
struct TupleHolder {
    std::tuple<Args...> args;
    std::tuple<Args...>&& operator()() { return std::move(args); }
};

}  // namespace internal

template <class T, class CtorArgsGetter>
class OnDemand {
    DISALLOW_COPY_AND_ASSIGN(OnDemand);

public:
    OnDemand(CtorArgsGetter&& argsGetter)
        : mCtorArgsGetter(std::move(argsGetter)) {}

    ~OnDemand() { clear(); }

    OnDemand(OnDemand&& other)
        : mCtorArgsGetter(std::move(other.mCtorArgsGetter)) {
        if (other.hasInstance()) {
            new (&mStorage) T(std::move(*other.asT()));
            mState.doneConstructing();
            other.clear();
        }
    }

    bool hasInstance() const { return !mState.inNoObjectState(); }

    const T& get() const { return *ptr(); }
    T& get() { return *ptr(); }

    const T* operator->() const { return ptr(); }
    T* operator->() { return ptr(); }

    const T& operator*() const { return get(); }
    T& operator*() { return get(); }

    T* ptr() {
        return const_cast<T*>(const_cast<const OnDemand*>(this)->ptr());
    }

    const T* ptr() const {
        if (mState.needConstruction()) {
            construct();
            mState.doneConstructing();
        }
        return asT();
    }

    void clear() {
        if (mState.needDestruction()) {
            asT()->~T();
            mState.doneDestroying();
        }
    }

private:
    T* asT() const { return reinterpret_cast<T*>(&mStorage); }

    void construct() const {
        using TupleType =
                typename std::decay<decltype(mCtorArgsGetter())>::type;
        constexpr int tupleSize = std::tuple_size<TupleType>::value;
        constructImpl(internal::MakeSeqT<tupleSize>());
    }
    template <int... S>
    void constructImpl(internal::Seq<S...>) const {
        auto args = mCtorArgsGetter();
        (void)args;
        new (&mStorage) T(std::move(std::get<S>(std::move(args)))...);
    }

    using StorageT =
            typename std::aligned_storage<sizeof(T),
                                          std::alignment_of<T>::value>::type;

    alignas(double) mutable internal::LazyInstanceState mState = {};
    mutable StorageT mStorage;
    mutable CtorArgsGetter mCtorArgsGetter;
};

// A type alias to simplify defining an OnDemand<> object that will only be
// created from a list of constructor arguments.
template <class T, class... Args>
using OnDemandT = OnDemand<T, internal::TupleHolder<Args...>>;

// A type alias for defining an OnDemand<> object that needs to be class member
// but must delay constructor parameter evaluation.
// This one has a slight overhead of converting the ArgsGetter to std::function
// even if it was a lambda or some struct.
template <class T, class... Args>
using MemberOnDemandT = OnDemand<T, std::function<std::tuple<Args...>()>>;

// makeOnDemand() - factory functions for simpler OnDemand<> object creation,
//                  either from argument list or from a factory function.
template <class T,
          class ArgsGetter,
          class = enable_if<is_callable_with_args<ArgsGetter, void()>>>
OnDemand<T, ArgsGetter> makeOnDemand(ArgsGetter&& getter) {
    return {std::move(getter)};
}

template <class T>
OnDemandT<T> makeOnDemand() {
    return {{}};
}

template <class T,
          class Arg0,
          class = enable_if_c<!is_callable_with_args<Arg0, void()>::value>>
OnDemandT<T, Arg0> makeOnDemand(Arg0&& arg0) {
    return {internal::TupleHolder<Arg0>{std::make_tuple(arg0)}};
}

template <class T, class Arg0, class Arg1, class... Args>
OnDemandT<T, Arg0, Arg1, Args...> makeOnDemand(Arg0&& arg0,
                                               Arg1&& arg1,
                                               Args&&... args) {
    return {internal::TupleHolder<Arg0, Arg1, Args...>{
            std::make_tuple(arg0, arg1, args...)}};
}

}  // namespace base
}  // namespace android
