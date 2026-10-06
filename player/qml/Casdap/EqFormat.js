.pragma library

var outputs = ["headphones", "usb", "speaker"]
var fields = ["type", "freq", "gain", "q"]
var fieldTitles = ["type", "frequency", "gain", "q"]

function baseName(path) {
    var s = String(path)
    var slash = s.lastIndexOf("/")
    var name = slash >= 0 ? s.substring(slash + 1) : s
    return name.toLowerCase().endsWith(".txt") ? name.substring(0, name.length - 4) : name
}

function key(p) {
    return p === null || p === undefined || p.key === undefined || p.key === null ? "off" : String(p.key)
}

function listed(p) {
    var k = key(p)
    return k !== "off" && k !== "" && p.missing !== true
}

function profileText(p) {
    var k = key(p)
    if (k === "off" || k === "")
        return "off"
    if (k === "custom")
        return "custom"
    var name = p.name !== undefined && p.name !== null && String(p.name) !== "" ? String(p.name) : baseName(k)
    return p.missing === true ? name + " (missing)" : name
}

function signed(v, plus) {
    var r = Math.round(Number(v) * 10) / 10
    var s = Math.abs(r).toFixed(1)
    if (r < 0)
        return "-" + s
    return plus && r > 0 ? "+" + s : s
}

function preampText(v) {
    return signed(v, false) + " db"
}

function gainText(v) {
    return signed(v, true) + " db"
}

function freqText(f) {
    var r = Math.round(Number(f))
    if (r < 1000)
        return r + " hz"
    return String(Math.round(Number(f) / 100) / 10) + " khz"
}

function qText(q) {
    return Number(q).toFixed(2)
}

function typeShort(t) {
    return t === "lowshelf" ? "ls" : t === "highshelf" ? "hs" : "pk"
}

function typeLong(t) {
    return t === "peak" ? "peak" : t === "lowshelf" ? "low shelf" : t === "highshelf" ? "high shelf" : "off"
}

function bandText(b) {
    if (b === null || b === undefined || b.type === undefined || String(b.type) === "off")
        return "off"
    return typeShort(String(b.type)) + " " + freqText(b.freq) + " " + gainText(b.gain) + " q " + qText(b.q)
}

function fieldText(b, field) {
    if (b === null || b === undefined)
        return ""
    if (field === "type")
        return typeLong(String(b.type))
    if (field === "freq")
        return freqText(b.freq)
    if (field === "gain")
        return gainText(b.gain)
    return qText(b.q)
}
