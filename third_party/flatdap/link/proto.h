#pragma once
#include <cstdint>

namespace fd::proto {

enum class Kind : uint8_t { U8, U16, U32, U64, I32, I64, Bool, Str, Bytes, Enum, Rec };

struct Const {
    const char* name;
    const char* value;
};

struct Code {
    const char* name;
    int value;
};

struct EnumValue {
    const char* kind;
    int value;
    const char* name;
};

struct Msg {
    const char* name;
    uint8_t id;
    const char* dir;
    const char* mode;
};

struct Tag {
    const char* scope;
    uint8_t num;
    const char* field;
    const char* type;
    Kind kind;
    bool list;
};

inline constexpr Const kConsts[] = {
    {"uuid", "6238ed73-a581-4335-9d8a-df1ecf688118"},
    {"magic", "0x50434446"},
    {"major", "1"},
    {"minor", "10"},
    {"headerBytes", "6"},
    {"tlvHeaderBytes", "3"},
    {"maxPayload", "16384"},
    {"flagReply", "0x01"},
    {"flagError", "0x02"},
    {"flagWant", "0x04"},
    {"flagReserved", "0xf8"},
    {"helloTimeoutMs", "5000"},
    {"requestTimeoutMs", "10000"},
    {"keepaliveIdleMs", "30000"},
    {"keepaliveBusyMs", "5000"},
    {"busyCapMs", "60000"},
    {"deadIntervals", "3"},
    {"pageFillPct", "75"},
    {"stateIntervalMs", "10000"},
    {"pushGateMs", "1000"},
    {"playMaxTracks", "500"},
    {"imgMaxBytes", "262144"},
    {"imgDataReserve", "16"},
    {"imgEndReplyMs", "5000"},
    {"refuseCount", "5"},
    {"refuseWindowMs", "10000"},
    {"backoffMs", "1000,2000,4000,8000,16000,30000"},
    {"artChunk", "16320"},
    {"artMaxPx", "512"},
    {"artMinPx", "64"},
    {"lrcMaxBytes", "15360"},
    {"updChunk", "16320"},
    {"lyrLineMax", "4096"},
};
inline constexpr int kConstCount = 33;

inline constexpr Code kErrs[] = {
    {"unknown", 1},
    {"badValue", 2},
    {"busy", 3},
    {"refused", 4},
    {"tooBig", 5},
    {"crc", 6},
    {"staleRev", 7},
    {"noLibrary", 8},
    {"noSpace", 9},
    {"notFound", 10},
    {"readOnly", 11},
    {"exists", 12},
};
inline constexpr int kErrCount = 12;

inline constexpr Code kFeatures[] = {
    {"settings", 0},
    {"eq", 1},
    {"library", 2},
    {"queue", 3},
    {"image", 4},
    {"art", 5},
    {"lyrics", 6},
    {"update", 7},
    {"clockAuto", 8},
    {"eqPresets", 9},
    {"artBlock", 10},
    {"lyricsGet", 11},
    {"artPick", 12},
    {"imgOrigin", 13},
    {"stateClock", 14},
};
inline constexpr int kFeatureCount = 15;

inline constexpr EnumValue kEnums[] = {
    {"SOURCE", 0, "local"},
    {"SOURCE", 1, "phone"},
    {"OUTPUT", 0, "wired"},
    {"OUTPUT", 1, "btHeadset"},
    {"OUTPUT", 2, "phoneReceiver"},
    {"REPEAT", 0, "off"},
    {"REPEAT", 1, "all"},
    {"REPEAT", 2, "one"},
    {"BYE_REASON", 0, "version"},
    {"BYE_REASON", 1, "replaced"},
    {"BYE_REASON", 2, "refused"},
    {"BYE_REASON", 3, "companionOff"},
    {"BYE_REASON", 4, "shutdown"},
    {"ROW_KIND", 0, "choice"},
    {"ROW_KIND", 1, "steps"},
    {"ROW_KIND", 2, "range"},
    {"ROW_KIND", 3, "toggle"},
    {"ROW_KIND", 4, "action"},
    {"ROW_KIND", 5, "draft"},
    {"ROW_KIND", 6, "info"},
    {"ROW_FLAG", 0, "readOnly"},
    {"ROW_FLAG", 1, "restart"},
    {"ROW_FLAG", 2, "boardOnly"},
    {"EQ_OUTPUT", 0, "wired"},
    {"EQ_OUTPUT", 1, "bt"},
    {"BAND_TYPE", 0, "off"},
    {"BAND_TYPE", 1, "peak"},
    {"BAND_TYPE", 2, "lowShelf"},
    {"BAND_TYPE", 3, "highShelf"},
    {"VIEW", 0, "artists"},
    {"VIEW", 1, "albums"},
    {"VIEW", 2, "songs"},
    {"VIEW", 3, "artistAlbums"},
    {"VIEW", 4, "albumTracks"},
    {"VIEW", 5, "playlists"},
    {"VIEW", 6, "folders"},
    {"VIEW", 7, "favorites"},
    {"TRANSPORT_CMD", 0, "toggle"},
    {"TRANSPORT_CMD", 1, "play"},
    {"TRANSPORT_CMD", 2, "pause"},
    {"TRANSPORT_CMD", 3, "next"},
    {"TRANSPORT_CMD", 4, "prev"},
    {"TRANSPORT_CMD", 5, "seek"},
    {"TRANSPORT_CMD", 6, "shuffle"},
    {"TRANSPORT_CMD", 7, "repeat"},
    {"DRAFT_KIND", 0, "usbMode"},
    {"DRAFT_KIND", 1, "btCodec"},
    {"DRAFT_KIND", 2, "sleep"},
    {"DRAFT_KIND", 3, "clock"},
    {"BT_ROLE", 0, "receiver"},
    {"BT_ROLE", 1, "headphones"},
    {"DRAFT_STATUS", 0, "done"},
    {"DRAFT_STATUS", 1, "pending"},
    {"QUEUE_CMD", 0, "page"},
    {"QUEUE_CMD", 1, "playAt"},
    {"QUEUE_CMD", 2, "remove"},
    {"QUEUE_CMD", 3, "move"},
    {"QUEUE_CMD", 4, "playNext"},
    {"QUEUE_CMD", 5, "clear"},
    {"ACTION_CMD", 0, "rescan"},
    {"ACTION_CMD", 1, "shuffleLibrary"},
    {"ACTION_CMD", 2, "favorite"},
    {"IMG_KIND", 0, "artist"},
    {"ABORT_CODE", 0, "crc"},
    {"ABORT_CODE", 1, "badValue"},
    {"ABORT_CODE", 2, "noSpace"},
    {"ABORT_CODE", 3, "busy"},
    {"ABORT_CODE", 4, "offsetMismatch"},
    {"ABORT_CODE", 5, "superseded"},
    {"ABORT_CODE", 6, "writeError"},
    {"EQ_PRESET_OP", 0, "save"},
    {"EQ_PRESET_OP", 1, "remove"},
    {"EQ_PRESET_OP", 2, "rename"},
    {"BLOCK_CMD", 0, "blockImage"},
    {"BLOCK_CMD", 1, "wrongArtist"},
    {"LYRICS_KIND", 0, "synced"},
    {"LYRICS_KIND", 1, "plain"},
    {"IMG_ORIGIN", 0, "library"},
    {"IMG_ORIGIN", 1, "phone"},
};
inline constexpr int kEnumCount = 79;

inline constexpr Msg kMsgs[] = {
    {"HELLO", 0x01, "app", "req"},
    {"PING", 0x02, "both", "req"},
    {"BYE", 0x03, "dev", "push"},
    {"STATE", 0x10, "dev", "push"},
    {"SCHEMA", 0x11, "dev", "push"},
    {"SETTINGS", 0x12, "dev", "push"},
    {"EQ", 0x13, "dev", "push"},
    {"QUEUE", 0x14, "dev", "push"},
    {"LIB", 0x15, "dev", "push"},
    {"TRANSPORT", 0x20, "app", "req"},
    {"VOLUME", 0x21, "app", "req"},
    {"SET", 0x22, "app", "req"},
    {"DRAFT", 0x23, "app", "req"},
    {"EQ_SET", 0x24, "app", "req"},
    {"PLAY", 0x25, "app", "req"},
    {"QUEUE_OP", 0x26, "app", "req"},
    {"LIB_GET", 0x27, "app", "req"},
    {"ACTION", 0x28, "app", "req"},
    {"ART_GET", 0x29, "app", "req"},
    {"LRC_PUT", 0x2a, "app", "req"},
    {"LRC_DEL", 0x2b, "app", "req"},
    {"UPD_BEGIN", 0x2c, "app", "req"},
    {"UPD_DATA", 0x2d, "app", "push"},
    {"UPD_END", 0x2e, "app", "req"},
    {"UPD_ABORT", 0x2f, "dev", "push"},
    {"IMG_BEGIN", 0x30, "app", "req"},
    {"IMG_DATA", 0x32, "app", "push"},
    {"IMG_END", 0x33, "app", "req"},
    {"IMG_LIST", 0x34, "app", "req"},
    {"IMG_DEL", 0x35, "app", "req"},
    {"IMG_ABORT", 0x36, "dev", "push"},
    {"EQ_PRESET", 0x37, "app", "req"},
    {"ART_BLOCK", 0x38, "dev", "req"},
    {"LYR_GET", 0x39, "app", "req"},
    {"ART_PICK", 0x3a, "dev", "req"},
    {"IMG_TAG", 0x3b, "app", "req"},
};
inline constexpr int kMsgCount = 36;

inline constexpr Tag kTags[] = {
    {"HELLO", 1, "magic", "u32", Kind::U32, false},
    {"HELLO", 2, "major", "u16", Kind::U16, false},
    {"HELLO", 3, "minor", "u16", Kind::U16, false},
    {"HELLO", 4, "appVersion", "str", Kind::Str, false},
    {"HELLO", 5, "features", "u64", Kind::U64, false},
    {"HELLO.reply", 1, "magic", "u32", Kind::U32, false},
    {"HELLO.reply", 2, "major", "u16", Kind::U16, false},
    {"HELLO.reply", 3, "minor", "u16", Kind::U16, false},
    {"HELLO.reply", 4, "board", "str", Kind::Str, false},
    {"HELLO.reply", 5, "version", "str", Kind::Str, false},
    {"HELLO.reply", 6, "screenW", "u16", Kind::U16, false},
    {"HELLO.reply", 7, "screenH", "u16", Kind::U16, false},
    {"HELLO.reply", 8, "backdropW", "u16", Kind::U16, false},
    {"HELLO.reply", 9, "backdropH", "u16", Kind::U16, false},
    {"HELLO.reply", 10, "maxPayload", "u32", Kind::U32, false},
    {"HELLO.reply", 11, "idleMs", "u32", Kind::U32, false},
    {"HELLO.reply", 12, "busyMs", "u32", Kind::U32, false},
    {"HELLO.reply", 13, "features", "u64", Kind::U64, false},
    {"HELLO.reply", 14, "libRev", "u32", Kind::U32, false},
    {"HELLO.reply", 15, "settingsRev", "u32", Kind::U32, false},
    {"HELLO.reply", 16, "snapRev", "u32", Kind::U32, false},
    {"BYE", 1, "reason", "BYE_REASON", Kind::Enum, false},
    {"BYE", 2, "major", "u16", Kind::U16, false},
    {"BYE", 3, "minor", "u16", Kind::U16, false},
    {"STATE", 1, "source", "SOURCE", Kind::Enum, false},
    {"STATE", 2, "output", "OUTPUT", Kind::Enum, false},
    {"STATE", 3, "track", "u64", Kind::U64, false},
    {"STATE", 4, "title", "str", Kind::Str, false},
    {"STATE", 5, "artist", "str", Kind::Str, false},
    {"STATE", 6, "artists", "str", Kind::Str, true},
    {"STATE", 7, "mbids", "str", Kind::Str, true},
    {"STATE", 8, "durationMs", "u32", Kind::U32, false},
    {"STATE", 9, "positionMs", "u32", Kind::U32, false},
    {"STATE", 10, "sampleMs", "u64", Kind::U64, false},
    {"STATE", 11, "sentMs", "u64", Kind::U64, false},
    {"STATE", 12, "playing", "bool", Kind::Bool, false},
    {"STATE", 13, "shuffle", "bool", Kind::Bool, false},
    {"STATE", 14, "repeat", "REPEAT", Kind::Enum, false},
    {"STATE", 15, "volume", "u16", Kind::U16, false},
    {"STATE", 16, "maxVolume", "u16", Kind::U16, false},
    {"STATE", 17, "battery", "u8", Kind::U8, false},
    {"STATE", 18, "queueRev", "u32", Kind::U32, false},
    {"STATE", 19, "slot", "u32", Kind::U32, false},
    {"STATE", 20, "slots", "u32", Kind::U32, false},
    {"STATE", 21, "libRev", "u32", Kind::U32, false},
    {"STATE", 22, "album", "str", Kind::Str, false},
    {"STATE", 23, "sleepLeftS", "u32", Kind::U32, false},
    {"STATE", 24, "charging", "bool", Kind::Bool, false},
    {"STATE", 25, "cardFreeMiB", "u32", Kind::U32, false},
    {"STATE", 26, "cardTotalMiB", "u32", Kind::U32, false},
    {"STATE", 27, "volumePct", "u8", Kind::U8, false},
    {"STATE", 28, "volumeStepPct", "u8", Kind::U8, false},
    {"STATE", 29, "lyrics", "bool", Kind::Bool, false},
    {"STATE", 30, "maxVolumePct", "u8", Kind::U8, false},
    {"STATE", 31, "favorite", "bool", Kind::Bool, false},
    {"STATE", 32, "snapRev", "u32", Kind::U32, false},
    {"STATE", 33, "lyricsPlain", "bool", Kind::Bool, false},
    {"STATE", 34, "lyricsCrc", "u32", Kind::U32, false},
    {"STATE", 35, "positionHeld", "bool", Kind::Bool, false},
    {"SCHEMA", 1, "pivot", "str", Kind::Str, false},
    {"SCHEMA", 2, "offset", "u32", Kind::U32, false},
    {"SCHEMA", 3, "total", "u32", Kind::U32, false},
    {"SCHEMA", 4, "more", "bool", Kind::Bool, false},
    {"SCHEMA", 5, "row", "Row", Kind::Rec, true},
    {"SCHEMA", 6, "rev", "u32", Kind::U32, false},
    {"SCHEMA", 7, "pivotIndex", "u8", Kind::U8, false},
    {"SCHEMA", 8, "pivotCount", "u8", Kind::U8, false},
    {"SETTINGS", 1, "rev", "u32", Kind::U32, false},
    {"SETTINGS", 2, "text", "str", Kind::Str, false},
    {"EQ", 1, "output", "EQ_OUTPUT", Kind::Enum, false},
    {"EQ", 2, "key", "str", Kind::Str, false},
    {"EQ", 3, "profile", "EqName", Kind::Rec, true},
    {"EQ", 4, "ints", "Profile", Kind::Rec, false},
    {"EQ", 5, "offset", "u32", Kind::U32, false},
    {"EQ", 6, "total", "u32", Kind::U32, false},
    {"EQ", 7, "more", "bool", Kind::Bool, false},
    {"QUEUE", 1, "rev", "u32", Kind::U32, false},
    {"QUEUE", 2, "offset", "u32", Kind::U32, false},
    {"QUEUE", 3, "total", "u32", Kind::U32, false},
    {"QUEUE", 4, "more", "bool", Kind::Bool, false},
    {"QUEUE", 5, "item", "QueueItem", Kind::Rec, true},
    {"LIB", 1, "libRev", "u32", Kind::U32, false},
    {"LIB", 2, "view", "VIEW", Kind::Enum, false},
    {"LIB", 3, "parent", "str", Kind::Str, false},
    {"LIB", 4, "offset", "u32", Kind::U32, false},
    {"LIB", 5, "total", "u32", Kind::U32, false},
    {"LIB", 6, "more", "bool", Kind::Bool, false},
    {"LIB", 7, "row", "LibRow", Kind::Rec, true},
    {"LIB", 8, "snapRev", "u32", Kind::U32, false},
    {"TRANSPORT", 1, "cmd", "TRANSPORT_CMD", Kind::Enum, false},
    {"TRANSPORT", 2, "seekMs", "u32", Kind::U32, false},
    {"TRANSPORT", 3, "on", "bool", Kind::Bool, false},
    {"TRANSPORT", 4, "repeat", "REPEAT", Kind::Enum, false},
    {"VOLUME", 1, "set", "u16", Kind::U16, false},
    {"VOLUME", 2, "step", "i32", Kind::I32, false},
    {"VOLUME", 3, "percent", "u8", Kind::U8, false},
    {"SET", 1, "text", "str", Kind::Str, false},
    {"SET.reply", 1, "rev", "u32", Kind::U32, false},
    {"SET.reply", 2, "kept", "str", Kind::Str, true},
    {"SET.reply", 3, "refused", "str", Kind::Str, true},
    {"DRAFT", 1, "kind", "DRAFT_KIND", Kind::Enum, false},
    {"DRAFT", 2, "value", "str", Kind::Str, false},
    {"DRAFT", 3, "role", "BT_ROLE", Kind::Enum, false},
    {"DRAFT", 4, "minutes", "u32", Kind::U32, false},
    {"DRAFT", 5, "unixS", "i64", Kind::I64, false},
    {"DRAFT", 6, "zoneS", "i32", Kind::I32, false},
    {"DRAFT", 7, "automatic", "bool", Kind::Bool, false},
    {"DRAFT.reply", 1, "status", "DRAFT_STATUS", Kind::Enum, false},
    {"EQ_SET", 1, "output", "EQ_OUTPUT", Kind::Enum, false},
    {"EQ_SET", 2, "key", "str", Kind::Str, false},
    {"EQ_SET", 3, "ints", "Profile", Kind::Rec, false},
    {"PLAY", 1, "view", "VIEW", Kind::Enum, false},
    {"PLAY", 2, "parent", "str", Kind::Str, false},
    {"PLAY", 3, "libRev", "u32", Kind::U32, false},
    {"PLAY", 4, "start", "u32", Kind::U32, false},
    {"PLAY", 5, "shuffle", "bool", Kind::Bool, false},
    {"PLAY", 6, "track", "u64", Kind::U64, true},
    {"PLAY", 7, "snapRev", "u32", Kind::U32, false},
    {"QUEUE_OP", 1, "cmd", "QUEUE_CMD", Kind::Enum, false},
    {"QUEUE_OP", 2, "rev", "u32", Kind::U32, false},
    {"QUEUE_OP", 3, "offset", "u32", Kind::U32, false},
    {"QUEUE_OP", 4, "count", "u32", Kind::U32, false},
    {"QUEUE_OP", 5, "slot", "u32", Kind::U32, false},
    {"QUEUE_OP", 6, "to", "u32", Kind::U32, false},
    {"QUEUE_OP", 7, "track", "u64", Kind::U64, true},
    {"LIB_GET", 1, "view", "VIEW", Kind::Enum, false},
    {"LIB_GET", 2, "parent", "str", Kind::Str, false},
    {"LIB_GET", 3, "offset", "u32", Kind::U32, false},
    {"LIB_GET", 4, "count", "u32", Kind::U32, false},
    {"LIB_GET", 5, "libRev", "u32", Kind::U32, false},
    {"LIB_GET", 6, "snapRev", "u32", Kind::U32, false},
    {"ACTION", 1, "cmd", "ACTION_CMD", Kind::Enum, false},
    {"ACTION", 2, "track", "u64", Kind::U64, false},
    {"ART_GET", 1, "track", "u64", Kind::U64, false},
    {"ART_GET", 2, "px", "u16", Kind::U16, false},
    {"ART_GET", 3, "offset", "u32", Kind::U32, false},
    {"ART_GET", 4, "have", "u32", Kind::U32, false},
    {"ART_GET.reply", 1, "crc", "u32", Kind::U32, false},
    {"ART_GET.reply", 2, "total", "u32", Kind::U32, false},
    {"ART_GET.reply", 3, "w", "u16", Kind::U16, false},
    {"ART_GET.reply", 4, "h", "u16", Kind::U16, false},
    {"ART_GET.reply", 5, "offset", "u32", Kind::U32, false},
    {"ART_GET.reply", 6, "data", "bytes", Kind::Bytes, false},
    {"LRC_PUT", 1, "track", "u64", Kind::U64, false},
    {"LRC_PUT", 2, "libRev", "u32", Kind::U32, false},
    {"LRC_PUT", 3, "text", "bytes", Kind::Bytes, false},
    {"LRC_PUT", 4, "crc", "u32", Kind::U32, false},
    {"LRC_PUT", 5, "snapRev", "u32", Kind::U32, false},
    {"LRC_DEL", 1, "track", "u64", Kind::U64, false},
    {"UPD_BEGIN", 1, "board", "str", Kind::Str, false},
    {"UPD_BEGIN", 2, "size", "u32", Kind::U32, false},
    {"UPD_BEGIN", 3, "md5", "str", Kind::Str, false},
    {"UPD_BEGIN.reply", 1, "offset", "u32", Kind::U32, false},
    {"UPD_DATA", 1, "offset", "u32", Kind::U32, false},
    {"UPD_DATA", 2, "data", "bytes", Kind::Bytes, false},
    {"UPD_END", 1, "md5", "str", Kind::Str, false},
    {"UPD_ABORT", 1, "offset", "u32", Kind::U32, false},
    {"UPD_ABORT", 2, "code", "ABORT_CODE", Kind::Enum, false},
    {"IMG_BEGIN", 1, "kind", "IMG_KIND", Kind::Enum, false},
    {"IMG_BEGIN", 2, "name", "str", Kind::Str, false},
    {"IMG_BEGIN", 3, "mbid", "str", Kind::Str, false},
    {"IMG_BEGIN", 4, "total", "u32", Kind::U32, false},
    {"IMG_BEGIN", 5, "crc", "u32", Kind::U32, false},
    {"IMG_BEGIN", 6, "w", "u16", Kind::U16, false},
    {"IMG_BEGIN", 7, "h", "u16", Kind::U16, false},
    {"IMG_BEGIN", 8, "wrongArtist", "bool", Kind::Bool, false},
    {"IMG_BEGIN", 9, "source", "str", Kind::Str, false},
    {"IMG_BEGIN", 10, "credit", "str", Kind::Str, false},
    {"IMG_BEGIN", 11, "origin", "IMG_ORIGIN", Kind::Enum, false},
    {"IMG_BEGIN.reply", 1, "offset", "u32", Kind::U32, false},
    {"IMG_BEGIN.reply", 2, "crc", "u32", Kind::U32, false},
    {"IMG_DATA", 1, "offset", "u32", Kind::U32, false},
    {"IMG_DATA", 2, "data", "bytes", Kind::Bytes, false},
    {"IMG_END", 1, "crc", "u32", Kind::U32, false},
    {"IMG_LIST", 1, "offset", "u32", Kind::U32, false},
    {"IMG_LIST", 2, "count", "u32", Kind::U32, false},
    {"IMG_LIST.reply", 1, "offset", "u32", Kind::U32, false},
    {"IMG_LIST.reply", 2, "total", "u32", Kind::U32, false},
    {"IMG_LIST.reply", 3, "more", "bool", Kind::Bool, false},
    {"IMG_LIST.reply", 4, "entry", "ImgEntry", Kind::Rec, true},
    {"IMG_LIST.reply", 5, "bytes", "u64", Kind::U64, false},
    {"IMG_LIST.reply", 6, "cap", "u64", Kind::U64, false},
    {"IMG_DEL", 1, "name", "str", Kind::Str, false},
    {"IMG_ABORT", 1, "name", "str", Kind::Str, false},
    {"IMG_ABORT", 2, "offset", "u32", Kind::U32, false},
    {"IMG_ABORT", 3, "code", "ABORT_CODE", Kind::Enum, false},
    {"EQ_PRESET", 1, "op", "EQ_PRESET_OP", Kind::Enum, false},
    {"EQ_PRESET", 2, "output", "EQ_OUTPUT", Kind::Enum, false},
    {"EQ_PRESET", 3, "key", "str", Kind::Str, false},
    {"EQ_PRESET", 4, "name", "str", Kind::Str, false},
    {"EQ_PRESET", 5, "ints", "Profile", Kind::Rec, false},
    {"EQ_PRESET", 6, "replace", "bool", Kind::Bool, false},
    {"EQ_PRESET.reply", 1, "key", "str", Kind::Str, false},
    {"ART_BLOCK", 1, "cmd", "BLOCK_CMD", Kind::Enum, false},
    {"ART_BLOCK", 2, "name", "str", Kind::Str, false},
    {"ART_BLOCK", 3, "mbid", "str", Kind::Str, false},
    {"ART_BLOCK", 4, "crc", "u32", Kind::U32, false},
    {"LYR_GET", 1, "track", "u64", Kind::U64, false},
    {"LYR_GET", 2, "offset", "u32", Kind::U32, false},
    {"LYR_GET", 3, "have", "u32", Kind::U32, false},
    {"LYR_GET.reply", 1, "crc", "u32", Kind::U32, false},
    {"LYR_GET.reply", 2, "kind", "LYRICS_KIND", Kind::Enum, false},
    {"LYR_GET.reply", 3, "total", "u32", Kind::U32, false},
    {"LYR_GET.reply", 4, "offset", "u32", Kind::U32, false},
    {"LYR_GET.reply", 5, "more", "bool", Kind::Bool, false},
    {"LYR_GET.reply", 6, "line", "LyrLine", Kind::Rec, true},
    {"ART_PICK", 1, "name", "str", Kind::Str, false},
    {"ART_PICK", 2, "mbid", "str", Kind::Str, false},
    {"ART_PICK.reply", 1, "shown", "bool", Kind::Bool, false},
    {"IMG_TAG", 1, "name", "str", Kind::Str, false},
    {"IMG_TAG", 2, "origin", "IMG_ORIGIN", Kind::Enum, false},
    {"error", 1, "code", "u8", Kind::U8, false},
    {"error", 2, "text", "str", Kind::Str, false},
    {"error", 3, "refused", "str", Kind::Str, true},
    {"Row", 1, "key", "str", Kind::Str, false},
    {"Row", 2, "label", "str", Kind::Str, false},
    {"Row", 3, "pivot", "str", Kind::Str, false},
    {"Row", 4, "kind", "ROW_KIND", Kind::Enum, false},
    {"Row", 5, "value", "str", Kind::Str, true},
    {"Row", 6, "min", "i32", Kind::I32, false},
    {"Row", 7, "max", "i32", Kind::I32, false},
    {"Row", 8, "step", "i32", Kind::I32, false},
    {"Row", 9, "word", "str", Kind::Str, true},
    {"Row", 10, "flag", "ROW_FLAG", Kind::Enum, true},
    {"EqName", 1, "key", "str", Kind::Str, false},
    {"EqName", 2, "name", "str", Kind::Str, false},
    {"EqName", 3, "preset", "bool", Kind::Bool, false},
    {"Profile", 1, "preamp10", "i32", Kind::I32, false},
    {"Profile", 2, "band", "Band", Kind::Rec, true},
    {"Band", 1, "type", "BAND_TYPE", Kind::Enum, false},
    {"Band", 2, "grid", "i32", Kind::I32, false},
    {"Band", 3, "milliHz", "i32", Kind::I32, false},
    {"Band", 4, "gain10", "i32", Kind::I32, false},
    {"Band", 5, "q100", "i32", Kind::I32, false},
    {"QueueItem", 1, "track", "u64", Kind::U64, false},
    {"QueueItem", 2, "title", "str", Kind::Str, false},
    {"QueueItem", 3, "artists", "str", Kind::Str, true},
    {"QueueItem", 4, "mbids", "str", Kind::Str, true},
    {"QueueItem", 5, "album", "str", Kind::Str, false},
    {"QueueItem", 6, "durationMs", "u32", Kind::U32, false},
    {"QueueItem", 7, "lyrics", "bool", Kind::Bool, false},
    {"QueueItem", 8, "lyricsPlain", "bool", Kind::Bool, false},
    {"LibRow", 1, "name", "str", Kind::Str, false},
    {"LibRow", 2, "count", "u32", Kind::U32, false},
    {"LibRow", 3, "first", "u64", Kind::U64, false},
    {"LibRow", 4, "key", "str", Kind::Str, false},
    {"LibRow", 5, "mbid", "str", Kind::Str, false},
    {"ImgEntry", 1, "name", "str", Kind::Str, false},
    {"ImgEntry", 2, "mbid", "str", Kind::Str, false},
    {"ImgEntry", 3, "origin", "IMG_ORIGIN", Kind::Enum, true},
    {"LyrLine", 1, "ms", "u32", Kind::U32, false},
    {"LyrLine", 2, "text", "str", Kind::Str, false},
};
inline constexpr int kTagCount = 252;

inline constexpr const char* uuid = kConsts[0].value;
inline constexpr int64_t magic = 0x50434446;
inline constexpr int64_t major = 1;
inline constexpr int64_t minor = 10;
inline constexpr int64_t headerBytes = 6;
inline constexpr int64_t tlvHeaderBytes = 3;
inline constexpr int64_t maxPayload = 16384;
inline constexpr int64_t flagReply = 0x01;
inline constexpr int64_t flagError = 0x02;
inline constexpr int64_t flagWant = 0x04;
inline constexpr int64_t flagReserved = 0xf8;
inline constexpr int64_t helloTimeoutMs = 5000;
inline constexpr int64_t requestTimeoutMs = 10000;
inline constexpr int64_t keepaliveIdleMs = 30000;
inline constexpr int64_t keepaliveBusyMs = 5000;
inline constexpr int64_t busyCapMs = 60000;
inline constexpr int64_t deadIntervals = 3;
inline constexpr int64_t pageFillPct = 75;
inline constexpr int64_t stateIntervalMs = 10000;
inline constexpr int64_t pushGateMs = 1000;
inline constexpr int64_t playMaxTracks = 500;
inline constexpr int64_t imgMaxBytes = 262144;
inline constexpr int64_t imgDataReserve = 16;
inline constexpr int64_t imgEndReplyMs = 5000;
inline constexpr int64_t refuseCount = 5;
inline constexpr int64_t refuseWindowMs = 10000;
inline constexpr const char* backoffMs = kConsts[26].value;
inline constexpr int64_t artChunk = 16320;
inline constexpr int64_t artMaxPx = 512;
inline constexpr int64_t artMinPx = 64;
inline constexpr int64_t lrcMaxBytes = 15360;
inline constexpr int64_t updChunk = 16320;
inline constexpr int64_t lyrLineMax = 4096;

namespace err {
inline constexpr uint8_t unknown = 1;
inline constexpr uint8_t badValue = 2;
inline constexpr uint8_t busy = 3;
inline constexpr uint8_t refused = 4;
inline constexpr uint8_t tooBig = 5;
inline constexpr uint8_t crc = 6;
inline constexpr uint8_t staleRev = 7;
inline constexpr uint8_t noLibrary = 8;
inline constexpr uint8_t noSpace = 9;
inline constexpr uint8_t notFound = 10;
inline constexpr uint8_t readOnly = 11;
inline constexpr uint8_t exists = 12;
}

namespace feature {
inline constexpr int settings = 0;
inline constexpr int eq = 1;
inline constexpr int library = 2;
inline constexpr int queue = 3;
inline constexpr int image = 4;
inline constexpr int art = 5;
inline constexpr int lyrics = 6;
inline constexpr int update = 7;
inline constexpr int clockAuto = 8;
inline constexpr int eqPresets = 9;
inline constexpr int artBlock = 10;
inline constexpr int lyricsGet = 11;
inline constexpr int artPick = 12;
inline constexpr int imgOrigin = 13;
inline constexpr int stateClock = 14;
}

namespace msg {
inline constexpr uint8_t HELLO = 0x01;
inline constexpr uint8_t PING = 0x02;
inline constexpr uint8_t BYE = 0x03;
inline constexpr uint8_t STATE = 0x10;
inline constexpr uint8_t SCHEMA = 0x11;
inline constexpr uint8_t SETTINGS = 0x12;
inline constexpr uint8_t EQ = 0x13;
inline constexpr uint8_t QUEUE = 0x14;
inline constexpr uint8_t LIB = 0x15;
inline constexpr uint8_t TRANSPORT = 0x20;
inline constexpr uint8_t VOLUME = 0x21;
inline constexpr uint8_t SET = 0x22;
inline constexpr uint8_t DRAFT = 0x23;
inline constexpr uint8_t EQ_SET = 0x24;
inline constexpr uint8_t PLAY = 0x25;
inline constexpr uint8_t QUEUE_OP = 0x26;
inline constexpr uint8_t LIB_GET = 0x27;
inline constexpr uint8_t ACTION = 0x28;
inline constexpr uint8_t ART_GET = 0x29;
inline constexpr uint8_t LRC_PUT = 0x2a;
inline constexpr uint8_t LRC_DEL = 0x2b;
inline constexpr uint8_t UPD_BEGIN = 0x2c;
inline constexpr uint8_t UPD_DATA = 0x2d;
inline constexpr uint8_t UPD_END = 0x2e;
inline constexpr uint8_t UPD_ABORT = 0x2f;
inline constexpr uint8_t IMG_BEGIN = 0x30;
inline constexpr uint8_t IMG_DATA = 0x32;
inline constexpr uint8_t IMG_END = 0x33;
inline constexpr uint8_t IMG_LIST = 0x34;
inline constexpr uint8_t IMG_DEL = 0x35;
inline constexpr uint8_t IMG_ABORT = 0x36;
inline constexpr uint8_t EQ_PRESET = 0x37;
inline constexpr uint8_t ART_BLOCK = 0x38;
inline constexpr uint8_t LYR_GET = 0x39;
inline constexpr uint8_t ART_PICK = 0x3a;
inline constexpr uint8_t IMG_TAG = 0x3b;
}

namespace val {
namespace SOURCE {
inline constexpr uint8_t local = 0;
inline constexpr uint8_t phone = 1;
}
namespace OUTPUT {
inline constexpr uint8_t wired = 0;
inline constexpr uint8_t btHeadset = 1;
inline constexpr uint8_t phoneReceiver = 2;
}
namespace REPEAT {
inline constexpr uint8_t off = 0;
inline constexpr uint8_t all = 1;
inline constexpr uint8_t one = 2;
}
namespace BYE_REASON {
inline constexpr uint8_t version = 0;
inline constexpr uint8_t replaced = 1;
inline constexpr uint8_t refused = 2;
inline constexpr uint8_t companionOff = 3;
inline constexpr uint8_t shutdown = 4;
}
namespace ROW_KIND {
inline constexpr uint8_t choice = 0;
inline constexpr uint8_t steps = 1;
inline constexpr uint8_t range = 2;
inline constexpr uint8_t toggle = 3;
inline constexpr uint8_t action = 4;
inline constexpr uint8_t draft = 5;
inline constexpr uint8_t info = 6;
}
namespace ROW_FLAG {
inline constexpr uint8_t readOnly = 0;
inline constexpr uint8_t restart = 1;
inline constexpr uint8_t boardOnly = 2;
}
namespace EQ_OUTPUT {
inline constexpr uint8_t wired = 0;
inline constexpr uint8_t bt = 1;
}
namespace BAND_TYPE {
inline constexpr uint8_t off = 0;
inline constexpr uint8_t peak = 1;
inline constexpr uint8_t lowShelf = 2;
inline constexpr uint8_t highShelf = 3;
}
namespace VIEW {
inline constexpr uint8_t artists = 0;
inline constexpr uint8_t albums = 1;
inline constexpr uint8_t songs = 2;
inline constexpr uint8_t artistAlbums = 3;
inline constexpr uint8_t albumTracks = 4;
inline constexpr uint8_t playlists = 5;
inline constexpr uint8_t folders = 6;
inline constexpr uint8_t favorites = 7;
}
namespace TRANSPORT_CMD {
inline constexpr uint8_t toggle = 0;
inline constexpr uint8_t play = 1;
inline constexpr uint8_t pause = 2;
inline constexpr uint8_t next = 3;
inline constexpr uint8_t prev = 4;
inline constexpr uint8_t seek = 5;
inline constexpr uint8_t shuffle = 6;
inline constexpr uint8_t repeat = 7;
}
namespace DRAFT_KIND {
inline constexpr uint8_t usbMode = 0;
inline constexpr uint8_t btCodec = 1;
inline constexpr uint8_t sleep = 2;
inline constexpr uint8_t clock = 3;
}
namespace BT_ROLE {
inline constexpr uint8_t receiver = 0;
inline constexpr uint8_t headphones = 1;
}
namespace DRAFT_STATUS {
inline constexpr uint8_t done = 0;
inline constexpr uint8_t pending = 1;
}
namespace QUEUE_CMD {
inline constexpr uint8_t page = 0;
inline constexpr uint8_t playAt = 1;
inline constexpr uint8_t remove = 2;
inline constexpr uint8_t move = 3;
inline constexpr uint8_t playNext = 4;
inline constexpr uint8_t clear = 5;
}
namespace ACTION_CMD {
inline constexpr uint8_t rescan = 0;
inline constexpr uint8_t shuffleLibrary = 1;
inline constexpr uint8_t favorite = 2;
}
namespace IMG_KIND {
inline constexpr uint8_t artist = 0;
}
namespace ABORT_CODE {
inline constexpr uint8_t crc = 0;
inline constexpr uint8_t badValue = 1;
inline constexpr uint8_t noSpace = 2;
inline constexpr uint8_t busy = 3;
inline constexpr uint8_t offsetMismatch = 4;
inline constexpr uint8_t superseded = 5;
inline constexpr uint8_t writeError = 6;
}
namespace EQ_PRESET_OP {
inline constexpr uint8_t save = 0;
inline constexpr uint8_t remove = 1;
inline constexpr uint8_t rename = 2;
}
namespace BLOCK_CMD {
inline constexpr uint8_t blockImage = 0;
inline constexpr uint8_t wrongArtist = 1;
}
namespace LYRICS_KIND {
inline constexpr uint8_t synced = 0;
inline constexpr uint8_t plain = 1;
}
namespace IMG_ORIGIN {
inline constexpr uint8_t library = 0;
inline constexpr uint8_t phone = 1;
}
}

namespace tag {
namespace HELLO {
inline constexpr uint8_t magic = 1;
inline constexpr uint8_t major = 2;
inline constexpr uint8_t minor = 3;
inline constexpr uint8_t appVersion = 4;
inline constexpr uint8_t features = 5;
}
namespace HELLO_reply {
inline constexpr uint8_t magic = 1;
inline constexpr uint8_t major = 2;
inline constexpr uint8_t minor = 3;
inline constexpr uint8_t board = 4;
inline constexpr uint8_t version = 5;
inline constexpr uint8_t screenW = 6;
inline constexpr uint8_t screenH = 7;
inline constexpr uint8_t backdropW = 8;
inline constexpr uint8_t backdropH = 9;
inline constexpr uint8_t maxPayload = 10;
inline constexpr uint8_t idleMs = 11;
inline constexpr uint8_t busyMs = 12;
inline constexpr uint8_t features = 13;
inline constexpr uint8_t libRev = 14;
inline constexpr uint8_t settingsRev = 15;
inline constexpr uint8_t snapRev = 16;
}
namespace BYE {
inline constexpr uint8_t reason = 1;
inline constexpr uint8_t major = 2;
inline constexpr uint8_t minor = 3;
}
namespace STATE {
inline constexpr uint8_t source = 1;
inline constexpr uint8_t output = 2;
inline constexpr uint8_t track = 3;
inline constexpr uint8_t title = 4;
inline constexpr uint8_t artist = 5;
inline constexpr uint8_t artists = 6;
inline constexpr uint8_t mbids = 7;
inline constexpr uint8_t durationMs = 8;
inline constexpr uint8_t positionMs = 9;
inline constexpr uint8_t sampleMs = 10;
inline constexpr uint8_t sentMs = 11;
inline constexpr uint8_t playing = 12;
inline constexpr uint8_t shuffle = 13;
inline constexpr uint8_t repeat = 14;
inline constexpr uint8_t volume = 15;
inline constexpr uint8_t maxVolume = 16;
inline constexpr uint8_t battery = 17;
inline constexpr uint8_t queueRev = 18;
inline constexpr uint8_t slot = 19;
inline constexpr uint8_t slots = 20;
inline constexpr uint8_t libRev = 21;
inline constexpr uint8_t album = 22;
inline constexpr uint8_t sleepLeftS = 23;
inline constexpr uint8_t charging = 24;
inline constexpr uint8_t cardFreeMiB = 25;
inline constexpr uint8_t cardTotalMiB = 26;
inline constexpr uint8_t volumePct = 27;
inline constexpr uint8_t volumeStepPct = 28;
inline constexpr uint8_t lyrics = 29;
inline constexpr uint8_t maxVolumePct = 30;
inline constexpr uint8_t favorite = 31;
inline constexpr uint8_t snapRev = 32;
inline constexpr uint8_t lyricsPlain = 33;
inline constexpr uint8_t lyricsCrc = 34;
inline constexpr uint8_t positionHeld = 35;
}
namespace SCHEMA {
inline constexpr uint8_t pivot = 1;
inline constexpr uint8_t offset = 2;
inline constexpr uint8_t total = 3;
inline constexpr uint8_t more = 4;
inline constexpr uint8_t row = 5;
inline constexpr uint8_t rev = 6;
inline constexpr uint8_t pivotIndex = 7;
inline constexpr uint8_t pivotCount = 8;
}
namespace SETTINGS {
inline constexpr uint8_t rev = 1;
inline constexpr uint8_t text = 2;
}
namespace EQ {
inline constexpr uint8_t output = 1;
inline constexpr uint8_t key = 2;
inline constexpr uint8_t profile = 3;
inline constexpr uint8_t ints = 4;
inline constexpr uint8_t offset = 5;
inline constexpr uint8_t total = 6;
inline constexpr uint8_t more = 7;
}
namespace QUEUE {
inline constexpr uint8_t rev = 1;
inline constexpr uint8_t offset = 2;
inline constexpr uint8_t total = 3;
inline constexpr uint8_t more = 4;
inline constexpr uint8_t item = 5;
}
namespace LIB {
inline constexpr uint8_t libRev = 1;
inline constexpr uint8_t view = 2;
inline constexpr uint8_t parent = 3;
inline constexpr uint8_t offset = 4;
inline constexpr uint8_t total = 5;
inline constexpr uint8_t more = 6;
inline constexpr uint8_t row = 7;
inline constexpr uint8_t snapRev = 8;
}
namespace TRANSPORT {
inline constexpr uint8_t cmd = 1;
inline constexpr uint8_t seekMs = 2;
inline constexpr uint8_t on = 3;
inline constexpr uint8_t repeat = 4;
}
namespace VOLUME {
inline constexpr uint8_t set = 1;
inline constexpr uint8_t step = 2;
inline constexpr uint8_t percent = 3;
}
namespace SET {
inline constexpr uint8_t text = 1;
}
namespace SET_reply {
inline constexpr uint8_t rev = 1;
inline constexpr uint8_t kept = 2;
inline constexpr uint8_t refused = 3;
}
namespace DRAFT {
inline constexpr uint8_t kind = 1;
inline constexpr uint8_t value = 2;
inline constexpr uint8_t role = 3;
inline constexpr uint8_t minutes = 4;
inline constexpr uint8_t unixS = 5;
inline constexpr uint8_t zoneS = 6;
inline constexpr uint8_t automatic = 7;
}
namespace DRAFT_reply {
inline constexpr uint8_t status = 1;
}
namespace EQ_SET {
inline constexpr uint8_t output = 1;
inline constexpr uint8_t key = 2;
inline constexpr uint8_t ints = 3;
}
namespace PLAY {
inline constexpr uint8_t view = 1;
inline constexpr uint8_t parent = 2;
inline constexpr uint8_t libRev = 3;
inline constexpr uint8_t start = 4;
inline constexpr uint8_t shuffle = 5;
inline constexpr uint8_t track = 6;
inline constexpr uint8_t snapRev = 7;
}
namespace QUEUE_OP {
inline constexpr uint8_t cmd = 1;
inline constexpr uint8_t rev = 2;
inline constexpr uint8_t offset = 3;
inline constexpr uint8_t count = 4;
inline constexpr uint8_t slot = 5;
inline constexpr uint8_t to = 6;
inline constexpr uint8_t track = 7;
}
namespace LIB_GET {
inline constexpr uint8_t view = 1;
inline constexpr uint8_t parent = 2;
inline constexpr uint8_t offset = 3;
inline constexpr uint8_t count = 4;
inline constexpr uint8_t libRev = 5;
inline constexpr uint8_t snapRev = 6;
}
namespace ACTION {
inline constexpr uint8_t cmd = 1;
inline constexpr uint8_t track = 2;
}
namespace ART_GET {
inline constexpr uint8_t track = 1;
inline constexpr uint8_t px = 2;
inline constexpr uint8_t offset = 3;
inline constexpr uint8_t have = 4;
}
namespace ART_GET_reply {
inline constexpr uint8_t crc = 1;
inline constexpr uint8_t total = 2;
inline constexpr uint8_t w = 3;
inline constexpr uint8_t h = 4;
inline constexpr uint8_t offset = 5;
inline constexpr uint8_t data = 6;
}
namespace LRC_PUT {
inline constexpr uint8_t track = 1;
inline constexpr uint8_t libRev = 2;
inline constexpr uint8_t text = 3;
inline constexpr uint8_t crc = 4;
inline constexpr uint8_t snapRev = 5;
}
namespace LRC_DEL {
inline constexpr uint8_t track = 1;
}
namespace UPD_BEGIN {
inline constexpr uint8_t board = 1;
inline constexpr uint8_t size = 2;
inline constexpr uint8_t md5 = 3;
}
namespace UPD_BEGIN_reply {
inline constexpr uint8_t offset = 1;
}
namespace UPD_DATA {
inline constexpr uint8_t offset = 1;
inline constexpr uint8_t data = 2;
}
namespace UPD_END {
inline constexpr uint8_t md5 = 1;
}
namespace UPD_ABORT {
inline constexpr uint8_t offset = 1;
inline constexpr uint8_t code = 2;
}
namespace IMG_BEGIN {
inline constexpr uint8_t kind = 1;
inline constexpr uint8_t name = 2;
inline constexpr uint8_t mbid = 3;
inline constexpr uint8_t total = 4;
inline constexpr uint8_t crc = 5;
inline constexpr uint8_t w = 6;
inline constexpr uint8_t h = 7;
inline constexpr uint8_t wrongArtist = 8;
inline constexpr uint8_t source = 9;
inline constexpr uint8_t credit = 10;
inline constexpr uint8_t origin = 11;
}
namespace IMG_BEGIN_reply {
inline constexpr uint8_t offset = 1;
inline constexpr uint8_t crc = 2;
}
namespace IMG_DATA {
inline constexpr uint8_t offset = 1;
inline constexpr uint8_t data = 2;
}
namespace IMG_END {
inline constexpr uint8_t crc = 1;
}
namespace IMG_LIST {
inline constexpr uint8_t offset = 1;
inline constexpr uint8_t count = 2;
}
namespace IMG_LIST_reply {
inline constexpr uint8_t offset = 1;
inline constexpr uint8_t total = 2;
inline constexpr uint8_t more = 3;
inline constexpr uint8_t entry = 4;
inline constexpr uint8_t bytes = 5;
inline constexpr uint8_t cap = 6;
}
namespace IMG_DEL {
inline constexpr uint8_t name = 1;
}
namespace IMG_ABORT {
inline constexpr uint8_t name = 1;
inline constexpr uint8_t offset = 2;
inline constexpr uint8_t code = 3;
}
namespace EQ_PRESET {
inline constexpr uint8_t op = 1;
inline constexpr uint8_t output = 2;
inline constexpr uint8_t key = 3;
inline constexpr uint8_t name = 4;
inline constexpr uint8_t ints = 5;
inline constexpr uint8_t replace = 6;
}
namespace EQ_PRESET_reply {
inline constexpr uint8_t key = 1;
}
namespace ART_BLOCK {
inline constexpr uint8_t cmd = 1;
inline constexpr uint8_t name = 2;
inline constexpr uint8_t mbid = 3;
inline constexpr uint8_t crc = 4;
}
namespace LYR_GET {
inline constexpr uint8_t track = 1;
inline constexpr uint8_t offset = 2;
inline constexpr uint8_t have = 3;
}
namespace LYR_GET_reply {
inline constexpr uint8_t crc = 1;
inline constexpr uint8_t kind = 2;
inline constexpr uint8_t total = 3;
inline constexpr uint8_t offset = 4;
inline constexpr uint8_t more = 5;
inline constexpr uint8_t line = 6;
}
namespace ART_PICK {
inline constexpr uint8_t name = 1;
inline constexpr uint8_t mbid = 2;
}
namespace ART_PICK_reply {
inline constexpr uint8_t shown = 1;
}
namespace IMG_TAG {
inline constexpr uint8_t name = 1;
inline constexpr uint8_t origin = 2;
}
namespace error {
inline constexpr uint8_t code = 1;
inline constexpr uint8_t text = 2;
inline constexpr uint8_t refused = 3;
}
namespace Row {
inline constexpr uint8_t key = 1;
inline constexpr uint8_t label = 2;
inline constexpr uint8_t pivot = 3;
inline constexpr uint8_t kind = 4;
inline constexpr uint8_t value = 5;
inline constexpr uint8_t min = 6;
inline constexpr uint8_t max = 7;
inline constexpr uint8_t step = 8;
inline constexpr uint8_t word = 9;
inline constexpr uint8_t flag = 10;
}
namespace EqName {
inline constexpr uint8_t key = 1;
inline constexpr uint8_t name = 2;
inline constexpr uint8_t preset = 3;
}
namespace Profile {
inline constexpr uint8_t preamp10 = 1;
inline constexpr uint8_t band = 2;
}
namespace Band {
inline constexpr uint8_t type = 1;
inline constexpr uint8_t grid = 2;
inline constexpr uint8_t milliHz = 3;
inline constexpr uint8_t gain10 = 4;
inline constexpr uint8_t q100 = 5;
}
namespace QueueItem {
inline constexpr uint8_t track = 1;
inline constexpr uint8_t title = 2;
inline constexpr uint8_t artists = 3;
inline constexpr uint8_t mbids = 4;
inline constexpr uint8_t album = 5;
inline constexpr uint8_t durationMs = 6;
inline constexpr uint8_t lyrics = 7;
inline constexpr uint8_t lyricsPlain = 8;
}
namespace LibRow {
inline constexpr uint8_t name = 1;
inline constexpr uint8_t count = 2;
inline constexpr uint8_t first = 3;
inline constexpr uint8_t key = 4;
inline constexpr uint8_t mbid = 5;
}
namespace ImgEntry {
inline constexpr uint8_t name = 1;
inline constexpr uint8_t mbid = 2;
inline constexpr uint8_t origin = 3;
}
namespace LyrLine {
inline constexpr uint8_t ms = 1;
inline constexpr uint8_t text = 2;
}
}

}
