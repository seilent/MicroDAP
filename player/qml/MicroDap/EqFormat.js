.pragma library

var outputs = ["headphones", "usb", "speaker"]
var maxBands = 10
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

function freqText(f, index) {
    var r = Math.round(Number(f))
    var offGrid = index !== undefined && index !== null && Number(index) < 0
    if (r < 1000 || (offGrid && r % 100 !== 0))
        return r + " hz"
    return String(Math.round(Number(f) / 100) / 10) + " khz"
}

var typedFields = ["preamp", "freq", "gain", "q"]

function entryText(field, value) {
    if (field === "freq")
        return String(Math.round(Number(value)))
    return Number(value).toFixed(field === "q" ? 2 : 1)
}

function typedRequest(field, title, value) {
    if (field === "freq")
        return { title: title, kind: "number", unit: "hz", text: entryText(field, value), minValue: 20, maxValue: 20000 }
    if (field === "q")
        return { title: title, kind: "number", extras: ".", decimals: 2, text: entryText(field, value), minValue: 0.1, maxValue: 10 }
    return { title: title, kind: "number", extras: "-.", decimals: 1, unit: "db", text: entryText(field, value),
        minValue: -24, maxValue: field === "preamp" ? 0 : 12 }
}

function qText(q) {
    return Number(q).toFixed(2)
}

var typeCodes = ({ peak: "pk", lowshelf: "ls", highshelf: "hs", bandpass: "bp", lowpass: "lp", highpass: "hp",
    allpass: "ap" })
var typeNames = ({ peak: "peak", lowshelf: "low shelf", highshelf: "high shelf", bandpass: "band pass",
    lowpass: "low pass", highpass: "high pass", allpass: "all pass" })

function typeShort(t) {
    return typeCodes.hasOwnProperty(t) ? typeCodes[t] : "pk"
}

function typeLong(t) {
    return typeNames.hasOwnProperty(t) ? typeNames[t] : "off"
}

function gainFree(t) {
    return t === "bandpass" || t === "lowpass" || t === "highpass" || t === "allpass"
}

function bandText(b) {
    if (b === null || b === undefined || b.type === undefined || String(b.type) === "off")
        return "off"
    const t = String(b.type)
    const gain = gainFree(t) ? "" : " " + gainText(b.gain)
    return typeShort(t) + " " + freqText(b.freq, b.freqIndex) + gain + " q " + qText(b.q)
}

function fieldInert(b, field) {
    return b !== null && b !== undefined && field === "gain" && gainFree(String(b.type))
}

function fieldText(b, field) {
    if (b === null || b === undefined)
        return ""
    if (field === "type")
        return typeLong(String(b.type))
    if (field === "freq")
        return freqText(b.freq, b.freqIndex)
    if (field === "gain")
        return fieldInert(b, field) ? "not used" : gainText(b.gain)
    return qText(b.q)
}

var dacFields = ["gain", "q", "preamp"]

function dacSuffix(field, value, ranges) {
    if (ranges === null || ranges === undefined || dacFields.indexOf(field) < 0 || ranges[field] === undefined)
        return ""
    var range = ranges[field]
    var v = field === "q" ? Number(qText(value)) : Number(signed(value, false))
    var c = Math.min(Number(range[1]), Math.max(Number(range[0]), v))
    if (c === v)
        return ""
    return " (dac " + (field === "q" ? qText(c) : signed(c, field === "gain")) + ")"
}
