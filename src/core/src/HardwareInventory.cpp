// MagicControl - hardware inventory: model helpers, JSON format and cache.
//
// The JSON writer and parser below are deliberately small and schema-specific.
// The inventory has a fixed shape - a timestamp and a list of sections, each a
// list of name/value strings - so a general JSON library would add a dependency
// and a build step for no benefit. The parser is strict: anything it does not
// recognise is rejected, because a silently half-read cache would show wrong
// facts about the hardware.

#include "MagicControl/HardwareInventory.h"
#include "MagicControl/FileStorage.h"
#include "MagicControl/Logging.h"
#include "MagicControl/OperationMessages.h"
#include "MagicControl/EcIdentityInventory.h"
#include "MagicControl/RomIdHistory.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace mc {
namespace {

/// Bumped whenever the on-disk shape changes, so an old file is rejected rather
/// than misread.
///
/// Version 2 also invalidates caches written before the text handling was
/// corrected: a stale snapshot can hold strings that were damaged by an earlier
/// build, and showing them again would look like a fresh defect.
// Version3 invalidates reports whose GPU capacity came from 32-bit WMI and
// could display a negative/truncated value on a discrete GPU.
constexpr int kCacheVersion = 3;

constexpr const char* kCacheFileName = "bios-inventory.json";
std::filesystem::path utf8Path(const std::string& text) {
    const auto* begin = reinterpret_cast<const char8_t*>(text.data());
    return std::filesystem::path(std::u8string(begin, begin + text.size()));
}


// ---------------------------------------------------------------------------
// JSON writing
// ---------------------------------------------------------------------------

/// Escapes a string for JSON output.
///
/// Non-ASCII bytes are passed through unchanged: the whole project is UTF-8, and
/// escaping Cyrillic into \u sequences would make the cache unreadable for no
/// gain.
void writeEscaped(std::string& out, const std::string& text) {
    out += '"';
    for (const char character : text) {
        switch (character) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += character;
                break;
        }
    }
    out += '"';
}

/// Appends `count` spaces, for readable nesting.
void writeIndent(std::string& out, int count) {
    out.append(static_cast<std::size_t>(count), ' ');
}

// ---------------------------------------------------------------------------
// JSON parsing
// ---------------------------------------------------------------------------

/// A minimal strict reader for the cache document.
class JsonReader {
public:
    explicit JsonReader(const std::string& text) : text_(text) {}

    bool parseDocument(HardwareInventory& inventory, int& version) {
        skipWhitespace();
        if (!consume('{')) {
            return false;
        }

        bool haveVersion = false;
        bool haveInventory = false;

        if (consume('}')) {
            return false;  // An empty document has neither field.
        }

        while (true) {
            std::string key;
            if (!parseString(key) || !consume(':')) {
                return false;
            }

            if (key == "version") {
                if (!parseInteger(version)) {
                    return false;
                }
                haveVersion = true;
            } else if (key == "inventory") {
                if (!parseInventory(inventory)) {
                    return false;
                }
                haveInventory = true;
            } else if (!skipValue()) {
                return false;
            }

            if (consume(',')) {
                continue;
            }
            if (consume('}')) {
                break;
            }
            return false;
        }

        skipWhitespace();
        return haveVersion && haveInventory && atEndIgnoringWhitespace();
    }

private:
    bool parseInventory(HardwareInventory& inventory) {
        skipWhitespace();
        if (!consume('{')) {
            return false;
        }
        if (consume('}')) {
            return false;  // An inventory with no timestamp is not usable.
        }

        while (true) {
            std::string key;
            if (!parseString(key) || !consume(':')) {
                return false;
            }

            if (key == "readAt") {
                if (!parseString(inventory.readAt)) {
                    return false;
                }
            } else if (key == "sections") {
                if (!parseSections(inventory.sections)) {
                    return false;
                }
            } else if (!skipValue()) {
                return false;
            }

            if (consume(',')) {
                continue;
            }
            if (consume('}')) {
                break;
            }
            return false;
        }

        return !inventory.readAt.empty() && !inventory.sections.empty();
    }

    bool parseSections(std::vector<InventorySection>& sections) {
        skipWhitespace();
        if (!consume('[')) {
            return false;
        }
        if (consume(']')) {
            return false;
        }

        while (true) {
            InventorySection section;
            if (!parseSection(section)) {
                return false;
            }
            sections.push_back(std::move(section));

            if (consume(',')) {
                continue;
            }
            if (consume(']')) {
                break;
            }
            return false;
        }
        return true;
    }

    bool parseSection(InventorySection& section) {
        skipWhitespace();
        if (!consume('{')) {
            return false;
        }

        bool haveTitle = false;
        bool haveRows = false;

        while (true) {
            std::string key;
            if (!parseString(key) || !consume(':')) {
                return false;
            }

            if (key == "title") {
                if (!parseString(section.title)) {
                    return false;
                }
                haveTitle = true;
            } else if (key == "rows") {
                if (!parseRows(section.rows)) {
                    return false;
                }
                haveRows = true;
            } else if (!skipValue()) {
                return false;
            }

            if (consume(',')) {
                continue;
            }
            if (consume('}')) {
                break;
            }
            return false;
        }

        return haveTitle && haveRows;
    }

    bool parseRows(std::vector<InventoryRow>& rows) {
        skipWhitespace();
        if (!consume('[')) {
            return false;
        }
        if (consume(']')) {
            return true;  // An empty section is allowed but unusual.
        }

        while (true) {
            InventoryRow row;
            if (!parseRow(row)) {
                return false;
            }
            rows.push_back(std::move(row));

            if (consume(',')) {
                continue;
            }
            if (consume(']')) {
                break;
            }
            return false;
        }
        return true;
    }

    bool parseRow(InventoryRow& row) {
        skipWhitespace();
        if (!consume('{')) {
            return false;
        }

        bool haveName = false;
        bool haveValue = false;

        while (true) {
            std::string key;
            if (!parseString(key) || !consume(':')) {
                return false;
            }

            if (key == "name") {
                if (!parseString(row.name)) {
                    return false;
                }
                haveName = true;
            } else if (key == "value") {
                if (!parseString(row.value)) {
                    return false;
                }
                haveValue = true;
            } else if (!skipValue()) {
                return false;
            }

            if (consume(',')) {
                continue;
            }
            if (consume('}')) {
                break;
            }
            return false;
        }

        return haveName && haveValue;
    }

    /// Skips a value whose key is not part of the schema, so a newer file with
    /// extra fields still loads when the version matches.
    bool skipValue() {
        skipWhitespace();
        if (atEndIgnoringWhitespace()) {
            return false;
        }

        const char character = text_[position_];
        if (character == '"') {
            std::string ignored;
            return parseString(ignored);
        }
        if (character == '{' || character == '[') {
            const char open = character;
            const char close = (character == '{') ? '}' : ']';
            int depth = 0;
            while (!atEndIgnoringWhitespace()) {
                const char current = text_[position_++];
                if (current == '"') {
                    // Skip a nested string without interpreting its escapes.
                    while (!atEndIgnoringWhitespace() && text_[position_] != '"') {
                        if (text_[position_] == '\\') {
                            ++position_;
                        }
                        ++position_;
                    }
                    if (!atEndIgnoringWhitespace()) {
                        ++position_;
                    }
                    continue;
                }
                if (current == open) {
                    ++depth;
                } else if (current == close) {
                    --depth;
                    if (depth == 0) {
                        return true;
                    }
                }
            }
            return false;
        }

        while (!atEndIgnoringWhitespace() && text_[position_] != ',' && text_[position_] != '}' &&
               text_[position_] != ']') {
            ++position_;
        }
        return true;
    }

    bool parseString(std::string& out) {
        skipWhitespace();
        if (!consume('"')) {
            return false;
        }

        out.clear();
        while (!atEnd()) {
            const char character = text_[position_++];
            if (character == '"') {
                return true;
            }
            if (character != '\\') {
                out += character;
                continue;
            }

            if (atEnd()) {
                return false;
            }
            const char escape = text_[position_++];
            switch (escape) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    // Only the BMP range is decoded; surrogate pairs are rare in
                    // this data and would need a second pair to combine.
                    if (position_ + 4 > text_.size()) {
                        return false;
                    }
                    unsigned int code = 0;
                    for (int index = 0; index < 4; ++index) {
                        const char digit = text_[position_++];
                        code <<= 4;
                        if (digit >= '0' && digit <= '9') {
                            code |= static_cast<unsigned int>(digit - '0');
                        } else if (digit >= 'a' && digit <= 'f') {
                            code |= static_cast<unsigned int>(digit - 'a' + 10);
                        } else if (digit >= 'A' && digit <= 'F') {
                            code |= static_cast<unsigned int>(digit - 'A' + 10);
                        } else {
                            return false;
                        }
                    }
                    // Encode the code point as UTF-8.
                    if (code < 0x80) {
                        out += static_cast<char>(code);
                    } else if (code < 0x800) {
                        out += static_cast<char>(0xC0 | (code >> 6));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    } else {
                        out += static_cast<char>(0xE0 | (code >> 12));
                        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    }
                    break;
                }
                default:
                    return false;
            }
        }
        return false;  // Unterminated string.
    }

    bool parseInteger(int& value) {
        skipWhitespace();
        const std::size_t start = position_;
        if (position_ < text_.size() && (text_[position_] == '-' || text_[position_] == '+')) {
            ++position_;
        }
        while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') {
            ++position_;
        }
        if (position_ == start) {
            return false;
        }
        try {
            value = std::stoi(text_.substr(start, position_ - start));
        } catch (const std::exception&) {
            return false;
        }
        return true;
    }

    bool consume(char expected) {
        skipWhitespace();
        if (position_ < text_.size() && text_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void skipWhitespace() {
        while (position_ < text_.size()) {
            const char character = text_[position_];
            if (character == ' ' || character == '\t' || character == '\n' || character == '\r') {
                ++position_;
            } else {
                break;
            }
        }
    }

    /// Reports whether the reader has consumed the whole document.
    ///
    /// This deliberately does NOT skip whitespace. It is called from inside the
    /// string parser's loop, and an earlier version called skipWhitespace here,
    /// which advanced the cursor past spaces that were part of a quoted value.
    /// Every space in every string was lost that way, so captions read
    /// "Модельизпрошивки" instead of "Модель из прошивки". Skipping whitespace
    /// belongs at token boundaries, not in an end-of-input test.
    [[nodiscard]] bool atEnd() const noexcept { return position_ >= text_.size(); }

    /// Reports whether only whitespace remains.
    [[nodiscard]] bool atEndIgnoringWhitespace() {
        skipWhitespace();
        return position_ >= text_.size();
    }

    const std::string& text_;
    std::size_t position_{0};
};

}  // namespace

// ---------------------------------------------------------------------------
// Model helpers
// ---------------------------------------------------------------------------

InventorySection& HardwareInventory::addSection(std::string title) {
    if(OperationMessages::capturing())logMessage("inventory","Раздел: "+title);
    sections.push_back(InventorySection{std::move(title), {}});
    return sections.back();
}

InventoryRow& InventorySection::addRow(std::string name, std::string value) {
    if(OperationMessages::capturing())logMessage("inventory",name+": "+value);
    rows.push_back(InventoryRow{std::move(name), std::move(value)});
    return rows.back();
}

bool HardwareInventory::hasSection(const std::string& title) const noexcept {
    for (const InventorySection& section : sections) {
        if (section.title == title) {
            return true;
        }
    }
    return false;
}

std::size_t HardwareInventory::rowCount() const noexcept {
    std::size_t total = 0;
    for (const InventorySection& section : sections) {
        total += section.rows.size();
    }
    return total;
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

std::string toJson(const HardwareInventory& inventory) {
    std::string out;
    out.reserve(1024 + inventory.rowCount() * 96);

    out += "{\n  \"version\": ";
    out += std::to_string(kCacheVersion);
    out += ",\n  \"inventory\": {\n    \"readAt\": ";
    writeEscaped(out, inventory.readAt);
    out += ",\n    \"sections\": [";

    for (std::size_t sectionIndex = 0; sectionIndex < inventory.sections.size(); ++sectionIndex) {
        const InventorySection& section = inventory.sections[sectionIndex];
        out += "\n      {\n        \"title\": ";
        writeEscaped(out, section.title);
        out += ",\n        \"rows\": [";

        for (std::size_t rowIndex = 0; rowIndex < section.rows.size(); ++rowIndex) {
            const InventoryRow& row = section.rows[rowIndex];
            out += "\n          { \"name\": ";
            writeEscaped(out, row.name);
            out += ", \"value\": ";
            writeEscaped(out, row.value);
            out += " }";
            if (rowIndex + 1 < section.rows.size()) {
                out += ',';
            }
        }

        out += "\n        ]\n      }";
        if (sectionIndex + 1 < inventory.sections.size()) {
            out += ',';
        }
    }

    out += "\n    ]\n  }\n}\n";
    return out;
}

std::optional<HardwareInventory> fromJson(const std::string& json) {
    JsonReader reader(json);
    HardwareInventory inventory;
    int version = 0;
    if (!reader.parseDocument(inventory, version)) {
        return std::nullopt;
    }
    if (version != kCacheVersion) {
        return std::nullopt;
    }
    return inventory;
}



// ---------------------------------------------------------------------------
// Cache
// ---------------------------------------------------------------------------

std::string InventoryCache::filePath() {
    // Owner25.10.2026: inventory travels with this laptop's archived logs.
    const auto path = (defaultLogDirectory() / kCacheFileName).u8string();
    return std::string(reinterpret_cast<const char*>(path.data()), path.size());
}

std::optional<HardwareInventory> InventoryCache::load() {
    try{
        const auto target=utf8Path(filePath());
        std::string lockError;
        const auto lease=detail::lockStorageFile(target.parent_path()/"locks"/"inventory.lock",true,lockError);
        if(!lease)return std::nullopt;
        std::error_code existsError;const bool present=std::filesystem::exists(target,existsError);
        // Import only if absent, preserving the root copy and any newer log cache.
        const auto source=present||existsError?target:utf8Path(executableDirectory())/kCacheFileName;
        std::ifstream file(source, std::ios::binary | std::ios::ate);
        if (file && (file.tellg() < 0 || file.tellg() > 2 * 1024 * 1024)) return std::nullopt;
        if (file) file.seekg(0);
        if (!file) {
            return std::nullopt;
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();
        if(file.bad()||buffer.bad())return std::nullopt;
        auto inventory=fromJson(buffer.str());
        if(inventory&&!present&&!existsError){
            std::error_code migrationError;std::filesystem::create_directories(target.parent_path(),migrationError);
            std::string error;
            if(migrationError||!detail::writeStorageTextAtomically(target,buffer.str(),error))
                logMessage("inventory","Legacy cache migration failed: "+(migrationError?migrationError.message():error));
        }
        // A legacy portable cache may have come from a different machine.
        // Never present Uniwill EC identities on another actual board. This
        // filters the in-memory report only; no EC access or change to the
        // stored snapshot is made by this filter.
        if(inventory&&!uniwillIdentityHistoryProfile(deviceProfile(readBoardDescriptor()))){
            for(auto& section:inventory->sections)std::erase_if(section.rows,[](const auto& row){return row.name==kEcProjectCaption||row.name==kEcPlatformCaption||row.name==kEcRomCaption;});
            std::erase_if(inventory->sections,[](const auto& section){return section.title==kEcIdentityInventoryMetadata;});
        }
        return inventory;
    }catch(...){return std::nullopt;}
}

bool InventoryCache::save(const HardwareInventory& inventory, std::string& error) {
    error.clear();
    try{
        const auto target=utf8Path(filePath());
        const auto json=toJson(inventory);
        if(json.size()>2*1024*1024){error="Inventory snapshot exceeds the 2 MiB cache limit";return false;}
        // Readers and GUI/CLI publishers serialize on the same stable OS lease.
        // Serialization is by completed publication, not by a guessed wall clock.
        const auto lease=detail::lockStorageFile(target.parent_path()/"locks"/"inventory.lock",true,error);
        if(!lease)return false;
        return detail::writeStorageTextAtomically(target,json,error);
    }catch(const std::exception& e){error=e.what();return false;}
}

}  // namespace mc
