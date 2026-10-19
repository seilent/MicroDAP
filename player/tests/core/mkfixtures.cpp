#include <gst/gst.h>

#include <taglib/fileref.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/mpegfile.h>
#include <taglib/tbytevector.h>
#include <taglib/tpropertymap.h>
#include <taglib/tstring.h>
#include <taglib/tstringlist.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr unsigned char png1x1[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
    0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89,
    0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54,
    0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00, 0x05, 0x00, 0x01,
    0x0D, 0x0A, 0x2D, 0xB4,
    0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};

constexpr int buffersFor2s = 87;

struct Track
{
    std::string file;
    std::string title;
    int number;
    int freq;
    std::vector<std::string> artists = {};
};

struct Album
{
    std::string dir;
    std::string title;
    std::string artist;
    int year;
    std::string genre;
    bool mp3;
    bool embedded;
    bool coverFile;
    std::vector<Track> tracks;
};

TagLib::String utf8(const std::string &s)
{
    return TagLib::String(s, TagLib::String::UTF8);
}

bool runPipeline(const std::string &description)
{
    GError *error = nullptr;
    GstElement *pipeline = gst_parse_launch(description.c_str(), &error);
    if (!pipeline) {
        std::fprintf(stderr, "mkfixtures: %s\n", error ? error->message : "gst_parse_launch failed");
        g_clear_error(&error);
        return false;
    }
    g_clear_error(&error);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    GstBus *bus = gst_element_get_bus(pipeline);
    GstMessage *message = gst_bus_timed_pop_filtered(
        bus, 60 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));

    bool ok = false;
    if (!message) {
        std::fprintf(stderr, "mkfixtures: pipeline timed out: %s\n", description.c_str());
    } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        GError *err = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_error(message, &err, &debug);
        std::fprintf(stderr, "mkfixtures: %s (%s)\n", err ? err->message : "error",
                     debug ? debug : "");
        g_clear_error(&err);
        g_free(debug);
    } else {
        ok = true;
    }
    if (message)
        gst_message_unref(message);
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    return ok;
}

bool encode(const fs::path &path, int freq, bool mp3)
{
    std::string description = "audiotestsrc num-buffers=" + std::to_string(buffersFor2s)
                              + " wave=sine freq=" + std::to_string(freq)
                              + " ! audioconvert ! audioresample ! ";
    description += mp3 ? "lamemp3enc" : "flacenc";
    description += " ! filesink location=\"" + path.string() + "\"";
    return runPipeline(description);
}

TagLib::PropertyMap propertiesFor(const Album &album, const Track &track)
{
    TagLib::PropertyMap props;
    TagLib::StringList artists;
    if (track.artists.empty())
        artists.append(utf8(album.artist));
    for (const std::string &artist : track.artists)
        artists.append(utf8(artist));
    props["TITLE"] = TagLib::StringList(utf8(track.title));
    props["ARTIST"] = artists;
    props["ALBUM"] = TagLib::StringList(utf8(album.title));
    props["ALBUMARTIST"] = TagLib::StringList(utf8(album.artist));
    props["TRACKNUMBER"] = TagLib::StringList(TagLib::String(std::to_string(track.number)));
    props["DATE"] = TagLib::StringList(TagLib::String(std::to_string(album.year)));
    props["GENRE"] = TagLib::StringList(utf8(album.genre));
    return props;
}

bool tagFlac(const fs::path &path, const Album &album, const Track &track)
{
    TagLib::FLAC::File file(path.c_str());
    if (!file.isValid()) {
        std::fprintf(stderr, "mkfixtures: taglib cannot open %s\n", path.c_str());
        return false;
    }
    file.setProperties(propertiesFor(album, track));
    if (album.embedded) {
        auto *picture = new TagLib::FLAC::Picture;
        picture->setData(TagLib::ByteVector(reinterpret_cast<const char *>(png1x1), sizeof png1x1));
        picture->setMimeType("image/png");
        picture->setType(TagLib::FLAC::Picture::FrontCover);
        picture->setWidth(1);
        picture->setHeight(1);
        picture->setColorDepth(32);
        file.addPicture(picture);
    }
    return file.save();
}

bool tagMp3(const fs::path &path, const Album &album, const Track &track)
{
    TagLib::MPEG::File file(path.c_str());
    if (!file.isValid()) {
        std::fprintf(stderr, "mkfixtures: taglib cannot open %s\n", path.c_str());
        return false;
    }
    file.setProperties(propertiesFor(album, track));
    return file.save();
}

bool writeCover(const fs::path &path)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(png1x1), sizeof png1x1);
    return out.good();
}

constexpr std::uint32_t dsd64Rate = 2822400;
constexpr std::uint32_t dsfBlock = 4096;
constexpr std::uint64_t dsdBytesPerChannel = dsd64Rate / 8;
constexpr char dsdSilence = 0x69;

void put(std::string &out, std::uint64_t value, int bytes, bool big)
{
    for (int i = 0; i < bytes; ++i) {
        const int shift = big ? (bytes - 1 - i) * 8 : i * 8;
        out.push_back(static_cast<char>((value >> shift) & 0xff));
    }
}

bool writeBytes(const fs::path &path, const std::string &bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

bool writeDsf(const fs::path &path)
{
    const std::uint64_t blocks = (dsdBytesPerChannel + dsfBlock - 1) / dsfBlock;
    const std::uint64_t data = blocks * dsfBlock * 2;
    std::string out = "DSD ";
    put(out, 28, 8, false);
    put(out, 28 + 52 + 12 + data, 8, false);
    put(out, 0, 8, false);
    out += "fmt ";
    put(out, 52, 8, false);
    put(out, 1, 4, false);
    put(out, 0, 4, false);
    put(out, 2, 4, false);
    put(out, 2, 4, false);
    put(out, dsd64Rate, 4, false);
    put(out, 1, 4, false);
    put(out, dsdBytesPerChannel * 8, 8, false);
    put(out, dsfBlock, 4, false);
    put(out, 0, 4, false);
    out += "data";
    put(out, 12 + data, 8, false);
    out.append(data, dsdSilence);
    return writeBytes(path, out);
}

bool writeDff(const fs::path &path)
{
    const std::string compression = "not compressed";
    std::string prop = "SND ";
    prop += "FS  ";
    put(prop, 4, 8, true);
    put(prop, dsd64Rate, 4, true);
    prop += "CHNL";
    put(prop, 10, 8, true);
    put(prop, 2, 2, true);
    prop += "SLFTSRGT";
    prop += "CMPR";
    put(prop, 4 + 1 + compression.size() + 1, 8, true);
    prop += "DSD ";
    put(prop, compression.size(), 1, true);
    prop += compression;
    prop.push_back('\0');
    const std::uint64_t data = dsdBytesPerChannel * 2;
    std::string body = "DSD ";
    body += "FVER";
    put(body, 4, 8, true);
    put(body, 0x01050000, 4, true);
    body += "PROP";
    put(body, prop.size(), 8, true);
    body += prop;
    body += "DSD ";
    put(body, data, 8, true);
    body.append(data, dsdSilence);
    std::string out = "FRM8";
    put(out, body.size(), 8, true);
    out += body;
    return writeBytes(path, out);
}

bool tagAny(const fs::path &path, const std::string &title)
{
    TagLib::FileRef file(path.c_str());
    if (file.isNull()) {
        std::fprintf(stderr, "mkfixtures: taglib cannot open %s\n", path.c_str());
        return false;
    }
    TagLib::PropertyMap props;
    props["TITLE"] = TagLib::StringList(utf8(title));
    props["ARTIST"] = TagLib::StringList(utf8("Format Band"));
    props["ALBUM"] = TagLib::StringList(utf8("Formats"));
    props["TRACKNUMBER"] = TagLib::StringList(TagLib::String(std::to_string(1)));
    file.setProperties(props);
    return file.save();
}

bool writeFormats(const fs::path &root)
{
    fs::create_directories(root);
    const std::string tone = "audiotestsrc num-buffers=" + std::to_string(buffersFor2s)
                             + " wave=sine freq=440 ! audioconvert ! audioresample ! ";
    return writeDsf(root / "dsd64.dsf") && tagAny(root / "dsd64.dsf", "DSF")
           && writeDff(root / "dsd64.dff") && tagAny(root / "dsd64.dff", "DFF")
           && runPipeline(tone + "vorbisenc ! oggmux ! filesink location=\"" + (root / "vorbis.oga").string() + "\"")
           && tagAny(root / "vorbis.oga", "OGA")
           && runPipeline(tone + "avenc_aac ! mp4mux ! filesink location=\"" + (root / "aac.mp4").string() + "\"")
           && tagAny(root / "aac.mp4", "MP4");
}

bool writeAlbum(const fs::path &root, const Album &album)
{
    const fs::path dir = root / album.dir;
    fs::create_directories(dir);
    for (const Track &track : album.tracks) {
        const fs::path path = dir / track.file;
        if (!encode(path, track.freq, album.mp3))
            return false;
        if (!(album.mp3 ? tagMp3(path, album, track) : tagFlac(path, album, track)))
            return false;
    }
    if (album.coverFile && !writeCover(dir / "cover.png"))
        return false;
    return true;
}

}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: mkfixtures OUTDIR [FORMATSDIR]\n");
        return 2;
    }
    const fs::path root = argv[1];
    gst_init(nullptr, nullptr);

    const std::vector<Album> albums = {
        {"Album One", "Album One", "Artist A", 2020, "Electronic", false, true, false,
         {{"01 One.flac", "One", 1, 440}, {"02 Two.flac", "Two", 2, 494},
          {"03 Three.flac", "Three", 3, 523, {"Artist A", "Artist C"}},
          {"04 Four.flac", "Four", 4, 554, {"Artist E, Artist F"}}}},
        {"アルバム二", "アルバム二", "アーティスト", 2021, "J-Pop", false, false, true,
         {{"01 一.flac", "一", 1, 587}, {"02 二.flac", "二", 2, 659}, {"03 三.flac", "三", 3, 698}}},
        {"Single", "Single", "The Band", 2019, "Rock", true, false, false,
         {{"01 Single.mp3", "Single", 1, 784}}},
    };

    for (const Album &album : albums) {
        if (!writeAlbum(root, album))
            return 1;
    }
    if (argc > 2 && !writeFormats(argv[2]))
        return 1;
    std::printf("mkfixtures: wrote %s\n", root.c_str());
    return 0;
}
