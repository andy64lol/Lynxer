//! Completion word lists for the Lynxer editor.
//!
//! `BUILTINS` is the interpreter's builtin registry (extracted from
//! `lynxer/builtins.cpp`), used both for autocomplete and highlighting.

pub const BUILTINS: &[&str] = &[
    "allOf",
    "anyOf",
    "assert",
    "asyncGather",
    "asyncPollClose",
    "asyncPollCreate",
    "asyncPollDispatch",
    "asyncPollModify",
    "asyncPollRegister",
    "asyncPollRemove",
    "asyncPollWait",
    "asyncRun",
    "asyncSleep",
    "asyncTimerCancel",
    "asyncTimerCreate",
    "asyncWakeupClose",
    "asyncWakeupCreate",
    "asyncWakeupSignal",
    "atomicAdd",
    "atomicLoad",
    "atomicStore",
    "bundledFile",
    "bundledFiles",
    "byte",
    "bytesAt",
    "bytesFromHex",
    "bytesLength",
    "bytesOf",
    "bytesToHex",
    "bytesToList",
    "bytesToStr",
    "charAt",
    "charCode",
    "charOf",
    "contains",
    "exceptionCode",
    "exceptionInfo",
    "ffiCall",
    "ffiCallback",
    "ffiCloseLibrary",
    "ffiFreeCallback",
    "ffiLoadLibrary",
    "ffiLookup",
    "filesystemChmod",
    "filesystemClose",
    "filesystemLink",
    "filesystemList",
    "filesystemMkdir",
    "filesystemOpen",
    "filesystemRead",
    "filesystemReadLink",
    "filesystemRemove",
    "filesystemRename",
    "filesystemStat",
    "filesystemWrite",
    "float32",
    "float64",
    "floatOf",
    "foreverDelay",
    "functionAddress",
    "getAddress",
    "getAddressValue",
    "input",
    "inputln",
    "int16",
    "int32",
    "int64",
    "int8",
    "intOf",
    "listAvg",
    "listClear",
    "listContains",
    "listCount",
    "listExtend",
    "listFirst",
    "listFlatten",
    "listGet",
    "listHead",
    "listIndex",
    "listInsert",
    "listJoin",
    "listJsonArray",
    "listJsonObject",
    "listLast",
    "listMax",
    "listMin",
    "listPop",
    "listPush",
    "listRemove",
    "listRepeat",
    "listSet",
    "listSlice",
    "listTail",
    "listToBytes",
    "listToTuple",
    "listUnique",
    "listZip",
    "lower",
    "memoryAllocate",
    "memoryAllocateZeroed",
    "memoryArrayAllocate",
    "memoryArrayGet",
    "memoryArrayLength",
    "memoryArraySet",
    "memoryArrayView",
    "memoryBlockAllocate",
    "memoryBlockGet",
    "memoryBlockLength",
    "memoryBlockSet",
    "memoryBlockView",
    "memoryCopy",
    "memoryFree",
    "memoryProtect",
    "memoryReadByte",
    "memoryReadEndian",
    "memoryReadFloat32",
    "memoryReadFloat64",
    "memoryReadInt16",
    "memoryReadInt32",
    "memoryReadInt64",
    "memoryReadInt8",
    "memoryReadUInt16",
    "memoryReadUInt32",
    "memoryReadUInt64",
    "memoryReadUInt8",
    "memoryReallocate",
    "memorySet",
    "memoryStructAlignment",
    "memoryStructAllocate",
    "memoryStructFieldCount",
    "memoryStructFieldOffset",
    "memoryStructFieldSize",
    "memoryStructFieldType",
    "memoryStructGet",
    "memoryStructSet",
    "memoryStructSize",
    "memoryTypeAlignment",
    "memoryTypeSize",
    "memoryViewGet",
    "memoryViewLength",
    "memoryViewSet",
    "memoryWriteByte",
    "memoryWriteEndian",
    "memoryWriteFloat32",
    "memoryWriteFloat64",
    "memoryWriteInt16",
    "memoryWriteInt32",
    "memoryWriteInt64",
    "memoryWriteInt8",
    "memoryWriteUInt16",
    "memoryWriteUInt32",
    "memoryWriteUInt64",
    "memoryWriteUInt8",
    "modifyAddressValue",
    "nativeCall",
    "nativeConditionClose",
    "nativeConditionCreate",
    "nativeConditionNotify",
    "nativeConditionNotifyAll",
    "nativeConditionWait",
    "nativeFunctionAddress",
    "nativeHandleAddress",
    "nativeHandleAllocate",
    "nativeHandleFree",
    "nativeHandleIsAlive",
    "nativeModuleClose",
    "nativeModuleConstant",
    "nativeModuleDependencies",
    "nativeModuleError",
    "nativeModuleFunction",
    "nativeModuleLoad",
    "nativeModuleName",
    "nativeModuleType",
    "nativeMutexClose",
    "nativeMutexCreate",
    "nativeMutexLock",
    "nativeMutexTryLock",
    "nativeMutexUnlock",
    "nativeSemaphoreClose",
    "nativeSemaphoreCreate",
    "nativeSemaphorePost",
    "nativeSemaphoreTryWait",
    "nativeSemaphoreWait",
    "nativeStructAlignment",
    "nativeStructAllocate",
    "nativeStructFieldCount",
    "nativeStructFieldOffset",
    "nativeStructFieldSize",
    "nativeStructFieldType",
    "nativeStructGet",
    "nativeStructSet",
    "nativeStructSize",
    "nativeThreadDetach",
    "nativeThreadIsAlive",
    "nativeThreadJoin",
    "nativeThreadJoinAll",
    "nativeThreadStart",
    "nativeThreadStatus",
    "nativeThreadYield",
    "nativeTypeAlignment",
    "networkingAccept",
    "networkingAddress",
    "networkingBind",
    "networkingBlocking",
    "networkingClose",
    "networkingConnect",
    "networkingListen",
    "networkingOpen",
    "networkingOption",
    "networkingReceive",
    "networkingResolve",
    "networkingSend",
    "networkingShutdown",
    "object",
    "overrideMain",
    "print",
    "printf",
    "println",
    "printlnf",
    "processClose",
    "processCloseInput",
    "processPoll",
    "processRead",
    "processSendSignal",
    "processSpawn",
    "processWait",
    "processWrite",
    "raise",
    "range",
    "replace",
    "returnLength",
    "returnType",
    "reverseList",
    "sentinel",
    "seqFromTo",
    "sizeOf",
    "sleep",
    "sortList",
    "soundIsPlaying",
    "soundLoad",
    "soundLoop",
    "soundPause",
    "soundPlay",
    "soundRelease",
    "soundResume",
    "soundSetVolume",
    "soundStop",
    "splitStr",
    "strOf",
    "substring",
    "sumOf",
    "suppressDeprecationWarning",
    "suppressForeverWarning",
    "trim",
    "tupleAll",
    "tupleAny",
    "tupleConcat",
    "tupleContains",
    "tupleCount",
    "tupleCreate",
    "tupleFirst",
    "tupleFlatten",
    "tupleGet",
    "tupleIndex",
    "tupleJoin",
    "tupleJsonArray",
    "tupleLast",
    "tupleLen",
    "tupleMax",
    "tupleMean",
    "tupleMin",
    "tupleReverse",
    "tupleSlice",
    "tupleSort",
    "tupleSortDesc",
    "tupleSum",
    "tupleToList",
    "tupleUnique",
    "tupleZip",
    "uint16",
    "uint32",
    "uint64",
    "uint8",
    "upper",
    "volatileRead",
    "volatileWrite",
];

/// Suggestions only appear once this many characters have been typed. This
/// keeps the popup from flashing after every keystroke.
pub const MIN_PREFIX: usize = 3;

/// Build a de-duplicated, prefix-filtered completion list. Keywords and types
/// come from the highlighter so the two never drift apart.
pub fn for_prefix(prefix: &str, document_words: &[String]) -> Vec<String> {
    if prefix.chars().count() < MIN_PREFIX {
        return Vec::new();
    }
    let lower = prefix.to_lowercase();
    let mut seen = std::collections::HashSet::new();
    let mut out: Vec<String> = Vec::new();

    let mut add = |word: &str| {
        if word.len() >= prefix.len()
            && word.to_lowercase().starts_with(&lower)
            && seen.insert(word.to_lowercase())
        {
            out.push(word.to_string());
        }
    };

    for word in crate::highlighter::KEYWORDS {
        add(word);
    }
    for word in crate::highlighter::TYPES {
        add(word);
    }
    for word in BUILTINS {
        add(word);
    }
    for word in document_words {
        add(word);
    }

    out.sort_by(|a, b| a.to_lowercase().cmp(&b.to_lowercase()));
    out.truncate(6);
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn filters_by_prefix_and_deduplicates() {
        let words = vec!["print".into(), "printMore".into(), "println".into()];
        let list = for_prefix("print", &words);
        assert!(list.contains(&"println".to_string()));
        assert!(list.contains(&"print".to_string()));
        assert_eq!(list.iter().filter(|w| *w == "print").count(), 1);
        assert!(list.len() <= 6);
    }

    #[test]
    fn short_prefixes_produce_no_suggestions() {
        assert!(for_prefix("", &[]).is_empty());
        assert!(for_prefix("p", &[]).is_empty());
        assert!(for_prefix("pr", &[]).is_empty());
        assert!(!for_prefix("pri", &[]).is_empty());
    }

    #[test]
    fn builtins_registry_is_populated() {
        assert!(BUILTINS.contains(&"println"));
        assert!(BUILTINS.contains(&"assert"));
        assert!(BUILTINS.len() > 200);
    }
}
