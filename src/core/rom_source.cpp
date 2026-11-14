/**
 * @file rom_source.cpp
 * @brief Leitura de pasta e de `.zip` (deflate próprio, sem dependências).
 */
#include "core/rom_source.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <system_error>

namespace rea::core {

namespace {

/// Lê o arquivo @p path inteiro para @p out. @return false se não abrir.
bool readWhole(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff size = f.tellg();
    if (size < 0) return false;
    // Um read só, do tamanho do arquivo (istreambuf_iterator lia byte a byte, sem reservar).
    out.resize(static_cast<std::size_t>(size));
    f.seekg(0);
    return size == 0 || static_cast<bool>(f.read(reinterpret_cast<char*>(out.data()), size));
}

/// Lê @p n bytes de @p f a partir de @p off para @p out. @return false se faltar arquivo.
bool readAt(std::ifstream& f, std::size_t off, std::size_t n, std::vector<uint8_t>& out) {
    out.resize(n);
    f.clear();
    f.seekg(static_cast<std::streamoff>(off));
    return n == 0 || static_cast<bool>(f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(n)));
}

/// Lê um inteiro de 16 bits little-endian de @p b no deslocamento @p o.
uint16_t le16(const std::vector<uint8_t>& b, std::size_t o) { return static_cast<uint16_t>(b[o] | b[o + 1] << 8); }
/// Lê um inteiro de 32 bits little-endian de @p b no deslocamento @p o.
uint32_t le32(const std::vector<uint8_t>& b, std::size_t o) {
    return static_cast<uint32_t>(b[o]) | static_cast<uint32_t>(b[o + 1]) << 8 | static_cast<uint32_t>(b[o + 2]) << 16 |
           static_cast<uint32_t>(b[o + 3]) << 24;
}

// --- deflate --------------------------------------------------------------------------------

/// Leitor de bits LSB-primeiro.
struct Bits {
    const uint8_t* p;  ///< Próximo byte do fluxo.
    std::size_t n;        ///< Tamanho do fluxo em bytes.
    std::size_t pos = 0;  ///< Próximo byte a ler.
    uint32_t buf = 0;  ///< Bits lidos e ainda não consumidos.
    int cnt = 0;  ///< Quantidade de bits em `buf`.
    bool bad = false;  ///< true se o fluxo acabou antes do esperado.
    /// Lê @p k bits (LSB primeiro). Fim do fluxo: marca @c bad e devolve 0.
    uint32_t get(int k) {
        while (cnt < k) {
            if (pos >= n) {
                bad = true;
                return 0;
            }
            buf |= static_cast<uint32_t>(p[pos++]) << cnt;
            cnt += 8;
        }
        const uint32_t v = buf & ((1u << k) - 1u);
        buf >>= k;
        cnt -= k;
        return v;
    }
    /// Descarta os bits até o próximo byte (blocos não comprimidos).
    void alignByte() {
        buf >>= cnt % 8;
        cnt -= cnt % 8;
    }
};

/// Código de Huffman canônico (contagem por comprimento + símbolos ordenados), como no `puff` de zlib.
struct Huffman {
    std::array<uint16_t, 16> count{};  ///< Quantos códigos há de cada comprimento.
    std::array<uint16_t, 320> symbol{};  ///< Símbolos ordenados por código.
    /// Monta o código a partir dos comprimentos @p lengths de @p n símbolos. @return false se o código for inválido.
    bool build(const uint8_t* lengths, int n) {
        count.fill(0);
        for (int s = 0; s < n; ++s) ++count[lengths[s]];
        if (count[0] == n) return true;  // código vazio (permitido para distâncias)
        int left = 1;
        for (int len = 1; len < 16; ++len) {
            left <<= 1;
            left -= count[len];
            if (left < 0) return false;  // excesso de códigos
        }
        std::array<uint16_t, 16> offs{};
        for (int len = 1; len < 15; ++len) offs[len + 1] = static_cast<uint16_t>(offs[len] + count[len]);
        for (int s = 0; s < n; ++s)
            if (lengths[s]) symbol[offs[lengths[s]]++] = static_cast<uint16_t>(s);
        return true;
    }
    /// Decodifica um símbolo de @p in. @return Símbolo, ou −1 se o fluxo acabou ou o código é inválido.
    int decode(Bits& in) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= static_cast<int>(in.get(1));
            if (in.bad) return -1;
            const int c = count[len];
            if (code - c < first) return symbol[index + (code - first)];
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
};

/// Comprimento base dos códigos 257–285 (RFC 1951, 3.2.5).
constexpr uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                   35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
/// Bits extras dos códigos de comprimento.
constexpr uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
/// Distância base dos códigos 0–29.
constexpr uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
/// Bits extras dos códigos de distância.
constexpr uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

/// Decodifica um bloco comprimido com os códigos @p lit e @p dist até o símbolo 256.
/// @param in Leitor de bits do fluxo.
/// @param out Recebe os bytes descompactados (também é a janela das distâncias).
/// @param lit Código dos literais e comprimentos.
/// @param dist Código das distâncias.
/// @param limit Tamanho máximo da saída (protege contra arquivos corrompidos que expandem sem fim).
/// @return false se o fluxo for inválido ou passar de @p limit.
bool inflateBlock(Bits& in, std::vector<uint8_t>& out, const Huffman& lit, const Huffman& dist, std::size_t limit) {
    for (;;) {
        if (out.size() > limit) return false;
        int sym = lit.decode(in);
        if (sym < 0) return false;
        if (sym < 256) {
            out.push_back(static_cast<uint8_t>(sym));
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29) return false;
            const std::size_t len = kLenBase[sym] + in.get(kLenExtra[sym]);
            const int ds = dist.decode(in);
            if (ds < 0 || ds >= 30) return false;
            const std::size_t d = kDistBase[ds] + in.get(kDistExtra[ds]);
            if (in.bad || d > out.size() || out.size() + len > limit) return false;
            const std::size_t from = out.size() - d;
            for (std::size_t i = 0; i < len; ++i) out.push_back(out[from + i]);
        }
    }
}

} // namespace

bool inflateRaw(const uint8_t* data, std::size_t size, std::vector<uint8_t>& out, std::size_t expected) {
    out.clear();
    // Com o tamanho esperado conhecido, a saída nunca passa dele; sem ele, o teto é a razão máxima do deflate (~1032:1).
    const std::size_t limit = expected ? expected : size * 1032 + 64;
    out.reserve(std::min(expected, limit));
    Bits in{data, size};
    static const auto fixed = [] {
        std::array<Huffman, 2> h;
        uint8_t l[288];
        for (int s = 0; s < 288; ++s) l[s] = s < 144 ? 8 : s < 256 ? 9 : s < 280 ? 7 : 8;
        h[0].build(l, 288);
        for (int s = 0; s < 30; ++s) l[s] = 5;
        h[1].build(l, 30);
        return h;
    }();
    bool last = false;
    while (!last) {
        last = in.get(1) != 0;
        const uint32_t type = in.get(2);
        if (in.bad) return false;
        if (type == 0) {  // guardado
            in.alignByte();
            in.buf = 0;
            in.cnt = 0;
            if (in.pos + 4 > size) return false;
            const uint16_t len = static_cast<uint16_t>(data[in.pos] | data[in.pos + 1] << 8);
            const uint16_t nlen = static_cast<uint16_t>(data[in.pos + 2] | data[in.pos + 3] << 8);
            in.pos += 4;
            if (len != static_cast<uint16_t>(~nlen) || in.pos + len > size || out.size() + len > limit) return false;
            out.insert(out.end(), data + in.pos, data + in.pos + len);
            in.pos += len;
        } else if (type == 1) {
            if (!inflateBlock(in, out, fixed[0], fixed[1], limit)) return false;
        } else if (type == 2) {  // Huffman dinâmico
            const int nlen = static_cast<int>(in.get(5)) + 257, ndist = static_cast<int>(in.get(5)) + 1;
            const int ncode = static_cast<int>(in.get(4)) + 4;
            if (in.bad || nlen > 286 || ndist > 30) return false;
            static constexpr uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            uint8_t lengths[320] = {};
            for (int i = 0; i < ncode; ++i) lengths[kOrder[i]] = static_cast<uint8_t>(in.get(3));
            Huffman code;
            if (!code.build(lengths, 19)) return false;
            int idx = 0;
            while (idx < nlen + ndist) {
                int sym = code.decode(in);
                if (sym < 0) return false;
                if (sym < 16) {
                    lengths[idx++] = static_cast<uint8_t>(sym);
                    continue;
                }
                uint8_t value = 0;
                int rep;
                if (sym == 16) {
                    if (idx == 0) return false;
                    value = lengths[idx - 1];
                    rep = 3 + static_cast<int>(in.get(2));
                } else if (sym == 17) {
                    rep = 3 + static_cast<int>(in.get(3));
                } else {
                    rep = 11 + static_cast<int>(in.get(7));
                }
                if (idx + rep > nlen + ndist) return false;
                while (rep--) lengths[idx++] = value;
            }
            if (lengths[256] == 0) return false;  // sem fim de bloco
            Huffman lit, dist;
            if (!lit.build(lengths, nlen) || !dist.build(lengths + nlen, ndist)) return false;
            if (!inflateBlock(in, out, lit, dist, limit)) return false;
        } else {
            return false;
        }
        if (in.bad) return false;
    }
    return true;
}

uint32_t crc32(const uint8_t* data, std::size_t size) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool RomSource::crc(const std::string& name, uint32_t& out) const {
    std::vector<uint8_t> data;
    if (!read(name, data)) return false;
    out = crc32(data.data(), data.size());
    return true;
}

// --- pasta -----------------------------------------------------------------------------------

bool DirRomSource::read(const std::string& name, std::vector<uint8_t>& out) const { return readWhole(dir_ / name, out); }

bool DirRomSource::has(const std::string& name) const {
    std::error_code ec;
    return std::filesystem::is_regular_file(dir_ / name, ec);
}

std::vector<std::string> DirRomSource::list() const {
    std::vector<std::string> names;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir_, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) names.push_back(it->path().filename().string());
    return names;
}

// --- zip -------------------------------------------------------------------------------------

ZipRomSource::ZipRomSource(std::filesystem::path zip) : zip_(std::move(zip)) {
    // Só o fim do arquivo e o diretório central são lidos (antes o .zip inteiro ia para a memória a cada
    // abertura, e ele é aberto pela CPU, pelo som e pela conferência da versão).
    std::ifstream f(zip_, std::ios::binary | std::ios::ate);
    if (!f) return;
    const std::streamoff end = f.tellg();
    if (end < 22) return;
    const auto file_size = static_cast<std::size_t>(end);
    // Fim do diretório central: assinatura 0x06054B50 nos últimos 22 + 65535 bytes.
    const std::size_t tail_at = file_size > 22 + 65535 ? file_size - 22 - 65535 : 0;
    std::vector<uint8_t> tail;
    if (!readAt(f, tail_at, file_size - tail_at, tail)) return;
    std::size_t eocd = std::string::npos;
    for (std::size_t o = tail.size() - 22 + 1; o-- > 0;) {
        if (le32(tail, o) == 0x06054B50u) {
            eocd = o;
            break;
        }
    }
    if (eocd == std::string::npos) return;
    const uint16_t count = le16(tail, eocd + 10);
    const std::size_t cd_size = le32(tail, eocd + 12), cd_at = le32(tail, eocd + 16);
    if (cd_at > file_size || cd_size > file_size - cd_at) return;
    std::vector<uint8_t> b;
    if (!readAt(f, cd_at, cd_size, b)) return;
    std::size_t o = 0;
    for (uint16_t i = 0; i < count; ++i) {
        if (o + 46 > b.size() || le32(b, o) != 0x02014B50u) return;
        Entry e{le16(b, o + 10), le32(b, o + 16), le32(b, o + 20), le32(b, o + 24), le32(b, o + 42)};
        const uint16_t nlen = le16(b, o + 28), xlen = le16(b, o + 30), clen = le16(b, o + 32);
        if (o + 46 + nlen > b.size()) return;
        // Entrada impossível (dados além do fim do arquivo ou expansão acima do máximo do deflate): ignorada, para
        // que um zip corrompido não peça alocações gigantes na leitura.
        const bool sane = std::size_t{e.local_offset} + 30 + e.compressed <= file_size &&
                          std::size_t{e.size} <= std::size_t{e.compressed} * 1032 + 64;
        std::string name(b.begin() + static_cast<std::ptrdiff_t>(o + 46), b.begin() + static_cast<std::ptrdiff_t>(o + 46 + nlen));
        if (const auto slash = name.find_last_of('/'); slash != std::string::npos) name = name.substr(slash + 1);
        if (!name.empty() && sane) entries_.emplace(name, e);
        o += 46u + nlen + xlen + clen;
    }
    ok_ = true;
}

bool ZipRomSource::read(const std::string& name, std::vector<uint8_t>& out) const {
    const auto it = entries_.find(name);
    if (it == entries_.end()) return false;
    const Entry& e = it->second;
    std::ifstream f(zip_, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> head(30);
    f.seekg(e.local_offset);
    if (!f.read(reinterpret_cast<char*>(head.data()), 30) || le32(head, 0) != 0x04034B50u) return false;
    f.seekg(static_cast<std::streamoff>(e.local_offset) + 30 + le16(head, 26) + le16(head, 28));
    std::vector<uint8_t> packed(e.compressed);
    if (!f.read(reinterpret_cast<char*>(packed.data()), static_cast<std::streamsize>(packed.size()))) return false;
    if (e.method == 0) {
        out = std::move(packed);
    } else if (e.method != 8 || !inflateRaw(packed.data(), packed.size(), out, e.size)) {
        return false;
    }
    return out.size() == e.size && crc32(out.data(), out.size()) == e.crc;
}

} // namespace rea::core
