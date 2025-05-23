/*
 * Tencent is pleased to support the open source community by making
 * MMKV available.
 *
 * Copyright (C) 2020 THL A29 Limited, a Tencent company.
 * All rights reserved.
 *
 * Licensed under the BSD 3-Clause License (the "License"); you may not use
 * this file except in compliance with the License. You may obtain a copy of
 * the License at
 *
 *       https://opensource.org/licenses/BSD-3-Clause
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <MMKV/MMKV.h>
#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#ifdef MMKV_WIN32
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#endif

using namespace mmkv;
using namespace std;
namespace py = pybind11;

static MMBuffer pyBytes2MMBuffer(const py::bytes &bytes) {
    char *buffer = nullptr;
    ssize_t length = 0;
    if (PYBIND11_BYTES_AS_STRING_AND_SIZE(bytes.ptr(), &buffer, &length) == 0) {
        return {buffer, static_cast<size_t>(length), MMBufferNoCopy};
    }
    return MMBuffer(0);
}

static function<void(MMKVLogLevel level, const char *file, int line, const char *function, const string &message)>
    g_logHandler = nullptr;
static void MyLogHandler(MMKVLogLevel level, const char *file, int line, const char *function, const string &message) {
    if (g_logHandler) {
        g_logHandler(level, file, line, function, message);
    }
}

static function<MMKVRecoverStrategic(const string &mmapID, MMKVErrorType errorType)> g_errorHandler = nullptr;
static MMKVRecoverStrategic MyErrorHandler(const string &mmapID, MMKVErrorType errorType) {
    if (g_errorHandler) {
        return g_errorHandler(mmapID, errorType);
    }
    return OnErrorDiscard;
}

static function<void(const string &mmapID)> g_contentHandler = nullptr;
static void MyContentChangeHandler(const std::string &mmapID) {
    if (g_contentHandler) {
        g_contentHandler(mmapID);
    }
}

PYBIND11_MODULE(mmkv, m) {
    m.doc() = "An efficient, small key-value storage framework developed by WeChat Team.";

    py::enum_<MMKVMode>(m, "MMKVMode", py::arithmetic())
        .value("SingleProcess", MMKVMode::MMKV_SINGLE_PROCESS)
        .value("MultiProcess", MMKVMode::MMKV_MULTI_PROCESS)
        .value("ReadOnly", MMKVMode::MMKV_READ_ONLY)
        .export_values();

    py::enum_<MMKVLogLevel>(m, "MMKVLogLevel")
        .value("NoLog", MMKVLogLevel::MMKVLogNone)
        .value("Debug", MMKVLogLevel::MMKVLogDebug)
        .value("Info", MMKVLogLevel::MMKVLogInfo)
        .value("Warning", MMKVLogLevel::MMKVLogWarning)
        .value("Error", MMKVLogLevel::MMKVLogError)
        .export_values();

    py::enum_<SyncFlag>(m, "SyncFlag")
        .value("Sync", SyncFlag::MMKV_SYNC)
        .value("ASync", SyncFlag::MMKV_ASYNC)
        .export_values();

    py::enum_<MMKVRecoverStrategic>(m, "MMKVRecoverStrategic")
        .value("OnErrorDiscard", MMKVRecoverStrategic::OnErrorDiscard)
        .value("OnErrorRecover", MMKVRecoverStrategic::OnErrorRecover)
        .export_values();

    py::enum_<MMKVErrorType>(m, "MMKVErrorType")
        .value("CRCCheckFail", MMKVErrorType::MMKVCRCCheckFail)
        .value("FileLength", MMKVErrorType::MMKVFileLength)
        .export_values();

    py::class_<NameSpace, unique_ptr<NameSpace>> clsNameSpace(m, "NameSpace");

    clsNameSpace.def("mmkvWithID", &NameSpace::mmkvWithID,
                "Parameters:\n"
                "  mmapID: all instances of the same mmapID share the same data and file storage\n"
                "  mode: pass MMKVMode.MultiProcess for a multi-process MMKV\n"
                "  cryptKey: pass a non-empty string for an encrypted MMKV, 16 bytes at most\n"
                "  expectedCapacity: the file size you expected when opening or creating file",
                py::arg("mmapID"), py::arg("mode") = MMKV_SINGLE_PROCESS, py::arg("cryptKey") = string(),
                py::arg("expectedCapacity") = 0);

    clsNameSpace.def("rootDir", &NameSpace::getRootDir, "get the root directory of NameSpace");

    clsNameSpace.def("backupOneToDirectory", &NameSpace::backupOneToDirectory,
        "backup one MMKV instance from the root dir of NameSpace to dstDir",
        py::arg("mmapID"), py::arg("dstDir"));

    clsNameSpace.def("restoreOneFromDirectory", &NameSpace::restoreOneFromDirectory,
        "restore one MMKV instance from srcDir to dstDir to the root dir of NameSpace",
        py::arg("mmapID"), py::arg("srcDir"));

    clsNameSpace.def("backupAllToDirectory", &NameSpace::backupAllToDirectory,
        "backup all MMKV instance from the root dir of NameSpace to dstDir", py::arg("dstDir"));

    clsNameSpace.def("restoreAllFromDirectory", &NameSpace::restoreAllFromDirectory,
        "restore all MMKV instance from srcDir to the root dir of NameSpace", py::arg("srcDir"));

    clsNameSpace.def("removeStorage", &NameSpace::removeStorage);
    clsNameSpace.def("isFileValid", &NameSpace::isFileValid);
    clsNameSpace.def("checkExist", &NameSpace::checkExist);

    py::class_<MMKV, unique_ptr<MMKV, py::nodelete>> clsMMKV(m, "MMKV");

    // TODO: not working
    // clsMMKV.def(py::init(&MMKV::mmkvWithID),
    //             py::arg("mmapID"),
    //             py::arg("mode") = MMKV_SINGLE_PROCESS,
    //             py::arg("cryptKey") = (string*) nullptr,
    //             py::arg("rootDir") = (string*) nullptr);

    clsMMKV.def(py::init([](const string &mmapID, MMKVMode mode, const string &cryptKey, const MMKVPath_t &rootDir,
                            const size_t expectedCapacity) {
                    string *cryptKeyPtr = (cryptKey.length() > 0) ? (string *) &cryptKey : nullptr;
                    MMKVPath_t *rootDirPtr = (rootDir.length() > 0) ? (MMKVPath_t *) &rootDir : nullptr;
                    return MMKV::mmkvWithID(mmapID, mode, cryptKeyPtr, rootDirPtr, expectedCapacity);
                }),
                "Parameters:\n"
                "  mmapID: all instances of the same mmapID share the same data and file storage\n"
                "  mode: pass MMKVMode.MultiProcess for a multi-process MMKV\n"
                "  cryptKey: pass a non-empty string for an encrypted MMKV, 16 bytes at most\n"
                "  rootDir: custom root directory",
                "  expectedCapacity: the file size you expected when opening or creating file", py::arg("mmapID"),
                py::arg("mode") = MMKV_SINGLE_PROCESS, py::arg("cryptKey") = string(), py::arg("rootDir") = string(),
                py::arg("expectedCapacity") = 0);

    clsMMKV.def("__eq__", [](MMKV &kv, const MMKV &other) { return kv.mmapID() == other.mmapID(); });

    clsMMKV.def_static(
        "initializeMMKV",
        [](const MMKVPath_t &rootDir, MMKVLogLevel logLevel, decltype(g_logHandler) logHandler) {
            if (logHandler) {
                g_logHandler = std::move(logHandler);
                MMKV::initializeMMKV(rootDir, logLevel, MyLogHandler);
            } else {
                MMKV::initializeMMKV(rootDir, logLevel, nullptr);
            }
        },
        "must call this before getting any MMKV instance", py::arg("rootDir"), py::arg("logLevel") = MMKVLogNone,
        py::arg("log_handler") = nullptr);

    clsMMKV.def_static(
        "defaultMMKV",
        [](MMKVMode mode, const string &cryptKey) {
            string *cryptKeyPtr = (cryptKey.length() > 0) ? (string *) &cryptKey : nullptr;
            return MMKV::defaultMMKV(mode, cryptKeyPtr);
        },
        "a generic purpose instance", py::arg("mode") = MMKV_SINGLE_PROCESS, py::arg("cryptKey") = string());

    clsMMKV.def_static("nameSpace", &MMKV::nameSpace, "get a namespace with custom root dir");
    clsMMKV.def_static("defaultNameSpace", &MMKV::defaultNameSpace, "identical with the original MMKV with the global root dir");

    clsMMKV.def("mmapID", &MMKV::mmapID);
    clsMMKV.def("isInterProcess", &MMKV::isMultiProcess);

    clsMMKV.def("cryptKey", &MMKV::cryptKey);
    clsMMKV.def("reKey", &MMKV::reKey,
                "transform plain text into encrypted text, or vice versa with an empty cryptKey\n"
                "Parameters:\n"
                "  newCryptKey: 16 bytes at most",
                py::arg("newCryptKey"));
    clsMMKV.def("checkReSetCryptKey", &MMKV::checkReSetCryptKey,
                "just reset cryptKey (will not encrypt or decrypt anything),\n"
                "usually you should call this method after other process reKey() a multi-process mmkv",
                py::arg("newCryptKey"));

    // TODO: Doesn't work, why?
    // clsMMKV.def("set", py::overload_cast<bool, const string&>(&MMKV::set), py::arg("value"), py::arg("key"));
    clsMMKV.def("set", (bool (MMKV::*)(bool, string_view))(&MMKV::set), "encode a boolean value", py::arg("value"),
                py::arg("key"));
    clsMMKV.def("set", (bool (MMKV::*)(bool, string_view, uint32_t))(&MMKV::set),
                "encode a boolean value with expiration", py::arg("value"), py::arg("key"), py::arg("expireDuration"));
    clsMMKV.def("set", (bool (MMKV::*)(int32_t, string_view))(&MMKV::set), "encode an int32 value", py::arg("value"),
                py::arg("key"));
    clsMMKV.def("set", (bool (MMKV::*)(int32_t, string_view, uint32_t))(&MMKV::set),
                "encode an int32 value with expiration", py::arg("value"), py::arg("key"), py::arg("expireDuration"));
    clsMMKV.def("set", (bool (MMKV::*)(uint32_t, string_view))(&MMKV::set), "encode an unsigned int32 value",
                py::arg("value"), py::arg("key"));
    clsMMKV.def("set", (bool (MMKV::*)(uint32_t, string_view, uint32_t))(&MMKV::set),
                "encode an unsigned int32 value with expiration", py::arg("value"), py::arg("key"),
                py::arg("expireDuration"));
    clsMMKV.def("set", (bool (MMKV::*)(int64_t, string_view))(&MMKV::set), "encode an int64 value", py::arg("value"),
                py::arg("key"));
    clsMMKV.def("set", (bool (MMKV::*)(int64_t, string_view, uint32_t))(&MMKV::set),
                "encode an int64 value with expiration", py::arg("value"), py::arg("key"), py::arg("expireDuration"));
    clsMMKV.def("set", (bool (MMKV::*)(uint64_t, string_view))(&MMKV::set), "encode an unsigned int64 value",
                py::arg("value"), py::arg("key"));
    clsMMKV.def("set", (bool (MMKV::*)(uint64_t, string_view, uint32_t))(&MMKV::set),
                "encode an unsigned int64 value with expiration", py::arg("value"), py::arg("key"),
                py::arg("expireDuration"));
    //clsMMKV.def("set", (bool (MMKV::*)(float, string_view))(&MMKV::set), py::arg("value"), py::arg("key"));
    clsMMKV.def("set", (bool (MMKV::*)(double, string_view))(&MMKV::set), "encode a float/double value",
                py::arg("value"), py::arg("key"));
    clsMMKV.def("set", (bool (MMKV::*)(double, string_view, uint32_t))(&MMKV::set),
                "encode a float/double value with expiration", py::arg("value"), py::arg("key"),
                py::arg("expireDuration"));
    //clsMMKV.def("set", (bool (MMKV::*)(const char*, string_view))(&MMKV::set), py::arg("value"), py::arg("key"));
    clsMMKV.def("set", (bool (MMKV::*)(const string &, string_view))(&MMKV::set),
                "encode an UTF-8 String/bytes value", py::arg("value"), py::arg("key"));
    clsMMKV.def("set", (bool (MMKV::*)(const string &, string_view, uint32_t))(&MMKV::set),
                "encode an UTF-8 String/bytes value with expiration", py::arg("value"), py::arg("key"),
                py::arg("expireDuration"));
#if PY_MAJOR_VERSION >= 3
    clsMMKV.def(
        "set", [](MMKV &kv, const py::bytes &value, const string &key) { return kv.set(pyBytes2MMBuffer(value), key); },
        "encode a bytes value", py::arg("value"), py::arg("key"));
    clsMMKV.def(
        "set",
        [](MMKV &kv, const py::bytes &value, const string &key, uint32_t expireDuration) {
            return kv.set(pyBytes2MMBuffer(value), key, expireDuration);
        },
        "encode a bytes value with expiration", py::arg("value"), py::arg("key"), py::arg("expireDuration"));
#endif

    clsMMKV.def("getBool", &MMKV::getBool, "decode a boolean value", py::arg("key"), py::arg("defaultValue") = false,
                py::arg("hasValue") = nullptr);
    clsMMKV.def("getInt", &MMKV::getInt32, "decode an int32 value", py::arg("key"), py::arg("defaultValue") = 0,
                py::arg("hasValue") = nullptr);
    clsMMKV.def("getUInt", &MMKV::getUInt32, "decode an unsigned int32 value", py::arg("key"),
                py::arg("defaultValue") = 0, py::arg("hasValue") = nullptr);
    clsMMKV.def("getLongInt", &MMKV::getInt64, "decode an int64 value", py::arg("key"), py::arg("defaultValue") = 0,
                py::arg("hasValue") = nullptr);
    clsMMKV.def("getLongUInt", &MMKV::getUInt64, "decode an unsigned int64 value", py::arg("key"),
                py::arg("defaultValue") = 0, py::arg("hasValue") = nullptr);
    //clsMMKV.def("getFloat", &MMKV::getFloat, py::arg("key"), py::arg("defaultValue") = 0);
    clsMMKV.def("getFloat", &MMKV::getDouble, "decode a float/double value", py::arg("key"),
                py::arg("defaultValue") = 0, py::arg("hasValue") = nullptr);
    clsMMKV.def(
        "getString",
        [](MMKV &kv, const string &key, const string &defaultValue) {
            string result;
            if (kv.getString(key, result)) {
                return result;
            }
            return defaultValue;
        },
        "decode an UTF-8 String/bytes value", py::arg("key"), py::arg("defaultValue") = string());

    clsMMKV.def(
        "getBytes",
        [](MMKV &kv, const string &key, const py::bytes &defaultValue) {
            MMBuffer result = kv.getBytes(key);
            if (result.length() > 0) {
                return py::bytes((const char *) result.getPtr(), result.length());
            }
            return defaultValue;
        },
        "decode a bytes value", py::arg("key"), py::arg("defaultValue") = py::bytes());

    clsMMKV.def("__contains__", &MMKV::containsKey, py::arg("key"));
    clsMMKV.def("keys", &MMKV::allKeys, py::arg("filterExpire") = false);

    clsMMKV.def("count", &MMKV::count, py::arg("filterExpire") = false);
    clsMMKV.def("totalSize", &MMKV::totalSize);
    clsMMKV.def("actualSize", &MMKV::actualSize);

    clsMMKV.def("remove", &MMKV::removeValueForKey, py::arg("key"));
    clsMMKV.def("remove", &MMKV::removeValuesForKeys, py::arg("keys"));
    clsMMKV.def("clearAll", &MMKV::clearAll, py::arg("keepSpace") = false, "remove all key-values");
    clsMMKV.def("trim", &MMKV::trim, "call this method after lots of removing if you care about disk usage");
    clsMMKV.def("importFrom", &MMKV::importFrom, "import all key-value items from others");
    clsMMKV.def("clearMemoryCache", &MMKV::clearMemoryCache, "call this method if you are facing memory-warning");

    clsMMKV.def("sync", &MMKV::sync, py::arg("flag") = MMKV_SYNC,
                "this call is not necessary unless you worry about unexpected shutdown of the machine (running out of "
                "battery, etc.)");

    clsMMKV.def("enableAutoKeyExpire", &MMKV::enableAutoKeyExpire, py::arg("expireDurationInSecond"),
                "turn on auto key expiration, passing 0 means never expire");
    clsMMKV.def("disableAutoKeyExpire", &MMKV::disableAutoKeyExpire, "turn off auto key expiration");

    clsMMKV.def("enableCompareBeforeSet", &MMKV::enableCompareBeforeSet, "turn on compare before set/update");
    clsMMKV.def("disableCompareBeforeSet", &MMKV::disableCompareBeforeSet, "turn off compare before set/update");

    clsMMKV.def("lock", &MMKV::lock, "get exclusive access, won't return until the lock is obtained");
    clsMMKV.def("unlock", &MMKV::unlock);
    clsMMKV.def("try_lock", &MMKV::try_lock, "try to get exclusive access");

    clsMMKV.def("isMultiProcess", &MMKV::isMultiProcess, "check multi-process mode");
    clsMMKV.def("isReadOnly", &MMKV::isReadOnly, "check read-only mode");

    clsMMKV.def("close", &MMKV::close, "close the instance");

    clsMMKV.def_static("rootDir", &MMKV::getRootDir, "get the root directory of MMKV");

    // log callback handler
    clsMMKV.def_static(
        "registerLogHandler",
        [](decltype(g_logHandler) callback) {
            g_logHandler = std::move(callback);
            MMKV::registerLogHandler(MyLogHandler);
        },
        "call this method to redirect MMKV's log,\n"
        "must call MMKV.unRegisterLogHandler() or MMKV.onExit() before exit\n"
        "Parameters:\n"
        "  log_handler: (logLevel: mmkv.MMKVLogLevel, file: str, line: int, function: str, message: str) -> None",
        py::arg("log_handler"));
    clsMMKV.def_static(
        "unRegisterLogHandler",
        [] {
            g_logHandler = nullptr;
            MMKV::unRegisterLogHandler();
        },
        "If you have registered a log handler, you must call this method or MMKV.onExit() before exit. "
        "Otherwise your app/script won't exit properly.");

    // error callback handler
    clsMMKV.def_static(
        "registerErrorHandler",
        [](decltype(g_errorHandler) callback) {
            g_errorHandler = std::move(callback);
            MMKV::registerErrorHandler(MyErrorHandler);
        },
        "call this method to handle MMKV failure,\n"
        "must call MMKV.unRegisterErrorHandler() or MMKV.onExit() before exit\n"
        "Parameters:\n"
        "  error_handler: (mmapID: str, errorType: mmkv.MMKVErrorType) -> mmkv.MMKVRecoverStrategic",
        py::arg("error_handler"));
    clsMMKV.def_static(
        "unRegisterErrorHandler",
        [] {
            g_errorHandler = nullptr;
            MMKV::unRegisterErrorHandler();
        },
        "If you have registered an error handler, you must call this method or MMKV.onExit() before exit. "
        "Otherwise your app/script won't exit properly.");

    // content change callback handler
    clsMMKV.def("checkContentChanged", &MMKV::checkContentChanged, "check if content been changed by other process");
    clsMMKV.def_static(
        "registerContentChangeHandler",
        [](decltype(g_contentHandler) callback) {
            g_contentHandler = std::move(callback);
            MMKV::registerContentChangeHandler(MyContentChangeHandler);
        },
        "register a content change handler,\n"
        "get notified when an MMKV instance has been changed by other process (not guarantee real-time notification),\n"
        "must call MMKV.unRegisterContentChangeHandler() or MMKV.onExit() before exit\n"
        "Parameters:\n"
        "  content_change_handler: (mmapID: str) -> None",
        py::arg("content_change_handler"));
    clsMMKV.def_static(
        "unRegisterContentChangeHandler",
        [] {
            g_contentHandler = nullptr;
            MMKV::unRegisterContentChangeHandler();
        },
        "If you have registered a content change handler, you must call this method or MMKV.onExit() before exit. "
        "Otherwise your app/script won't exit properly.");

    clsMMKV.def_static(
        "onExit",
        [] {
            MMKV::onExit();
            g_logHandler = nullptr;
            g_errorHandler = nullptr;
            g_contentHandler = nullptr;
        },
        "call this method before exit, especially if you have registered any callback handlers");

    clsMMKV.def_static(
        "backupOneToDirectory",
        [](const string &mmapID, const MMKVPath_t &dstDir, const MMKVPath_t &srcDir) {
            MMKVPath_t *srcDirPtr = (!srcDir.empty()) ? (MMKVPath_t *) &srcDir : nullptr;
            return MMKV::backupOneToDirectory(mmapID, dstDir, srcDirPtr);
        },
        "backup one MMKV instance from srcDir (default to the root dir of MMKV) to dstDir", py::arg("mmapID"),
        py::arg("dstDir"), py::arg("srcDir") = MMKVPath_t());

    clsMMKV.def_static(
        "restoreOneFromDirectory",
        [](const string &mmapID, const MMKVPath_t &srcDir, const MMKVPath_t &dstDir) {
            MMKVPath_t *dstDirPtr = (!dstDir.empty()) ? (MMKVPath_t *) &dstDir : nullptr;
            return MMKV::restoreOneFromDirectory(mmapID, srcDir, dstDirPtr);
        },
        "restore one MMKV instance from srcDir to dstDir (default to the root dir of MMKV)", py::arg("mmapID"),
        py::arg("srcDir"), py::arg("dstDir") = MMKVPath_t());

    clsMMKV.def_static(
        "backupAllToDirectory",
        [](const MMKVPath_t &dstDir, const MMKVPath_t &srcDir) {
            MMKVPath_t *srcDirPtr = (!srcDir.empty()) ? (MMKVPath_t *) &srcDir : nullptr;
            return MMKV::backupAllToDirectory(dstDir, srcDirPtr);
        },
        "backup all MMKV instance from srcDir (default to the root dir of MMKV) to dstDir", py::arg("dstDir"),
        py::arg("srcDir") = MMKVPath_t());

    clsMMKV.def_static(
        "restoreAllFromDirectory",
        [](const MMKVPath_t &srcDir, const MMKVPath_t &dstDir) {
            MMKVPath_t *dstDirPtr = (!dstDir.empty()) ? (MMKVPath_t *) &dstDir : nullptr;
            return MMKV::restoreAllFromDirectory(srcDir, dstDirPtr);
        },
        "restore all MMKV instance from srcDir to dstDir (default to the root dir of MMKV)", py::arg("srcDir"),
        py::arg("dstDir") = MMKVPath_t());

    clsMMKV.def_static(
        "removeStorage",
        [](const string &mmapID, const MMKVPath_t &rootDir) {
            MMKVPath_t *rootDirPtr = (!rootDir.empty()) ? (MMKVPath_t *) &rootDir : nullptr;
            return MMKV::removeStorage(mmapID, rootDirPtr);
        },
        "remove the storage of the MMKV, including the data file & meta file (.crc)", py::arg("mmapID"),
        py::arg("rootDir") = MMKVPath_t());

    clsMMKV.def_static("isFileValid",
       [](const string &mmapID, const MMKVPath_t &rootDir) {
           MMKVPath_t *rootDirPtr = (!rootDir.empty()) ? (MMKVPath_t *) &rootDir : nullptr;
           return MMKV::isFileValid(mmapID, rootDirPtr);
       },
       "detect if the MMKV file is valid or not", py::arg("mmapID"),
       py::arg("rootDir") = MMKVPath_t());

    clsMMKV.def_static("checkExist",
                       [](const string &mmapID, const MMKVPath_t &rootDir) {
                           MMKVPath_t *rootDirPtr = (!rootDir.empty()) ? (MMKVPath_t *) &rootDir : nullptr;
                           return MMKV::checkExist(mmapID, rootDirPtr);
                       },
                       "check if the MMKV file is exist or not", py::arg("mmapID"),
                       py::arg("rootDir") = MMKVPath_t());
}
