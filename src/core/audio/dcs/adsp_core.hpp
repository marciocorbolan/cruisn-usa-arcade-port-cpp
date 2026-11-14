#pragma once
/**
 * @file adsp_core.hpp
 * @brief Registradores e unidades de cálculo do ADSP-2105 (DSP da placa de som DCS).
 *
 * @details O programa do DSP (`dcs_program.cpp`) foi traduzido instrução a instrução, como o código da
 * CPU principal: cada linha C++ corresponde a uma instrução do original (endereço no comentário). Esta
 * classe reproduz o efeito de cada instrução nos registradores e nas flags:
 *
 * - **Registradores de dados** (16 bits): AX0, AX1, AY0, AY1, AR, AF, MX0, MX1, MY0, MY1, MF, SI, SE (8 bits
 *   com sinal), SR (SR1:SR0, 32 bits) e MR (MR2:MR1:MR0, 40 bits). Há um banco secundário (`ENA SEC_REG`),
 *   usado pelas interrupções.
 * - **ALU**: resultados em 16 bits com as flags AZ, AN, AV, AC (e AS no ABS); AR satura com `MSTAT` bit 3.
 * - **MAC**: produto 16×16 (SS/SU/US/UU), deslocado 1 bit à esquerda no modo fracionário (`MSTAT` bit 4 = 0);
 *   arredondamento (RND) não tendencioso; MV quando os bits 31–39 de MR não são todos iguais.
 * - **Deslocador**: LSHIFT/ASHIFT (HI/LO, com OR), EXP; contagem em SE ou imediata.
 * - **DAGs**: I0–I7 (14 bits), M0–M7 (com sinal), L0–L7 (buffers circulares); I0–I3 com bit-reverse
 *   (`MSTAT` bit 1).
 * - **Laços**: `DO … UNTIL CE` com a pilha de contadores (CNTR); a escrita em CNTR empilha o anterior.
 *
 * Referência de semântica: manual da família ADSP-2100 e o núcleo `adsp2100` do MAME (BSD-3-Clause,
 * Aaron Giles). Diferenças deliberadas em relação ao MAME: o produto da MAC é calculado sem estouro de
 * 32 bits e o arredondamento olha o resultado acumulado (como no manual), não só o produto.
 */

#include <array>
#include <cstdint>
#include <utility>

namespace rea::core::audio::dcs {

/// Memória vista pelo DSP (implementada pela DcsBoard).
class IAdspMemory {
public:
    virtual ~IAdspMemory() = default;
    virtual uint16_t dmRead(uint32_t addr) = 0;               ///< Lê a palavra de 16 bits em DM.
    virtual void dmWrite(uint32_t addr, uint16_t value) = 0;  ///< Grava a palavra de 16 bits em DM.
    virtual uint32_t pmRead(uint32_t addr) = 0;               ///< Lê a palavra de 24 bits em PM.
    virtual void pmWrite(uint32_t addr, uint32_t value) = 0;  ///< Grava a palavra de 24 bits em PM.
};

/// Banco de registradores de dados (primário ou secundário).
struct AdspRegs {
    uint16_t ax0 = 0;  ///< Entrada X0 da ALU.
    uint16_t ax1 = 0;  ///< Entrada X1 da ALU.
    uint16_t ay0 = 0;  ///< Entrada Y0 da ALU.
    uint16_t ay1 = 0;  ///< Entrada Y1 da ALU.
    uint16_t ar = 0;   ///< Resultado da ALU.
    uint16_t af = 0;   ///< Realimentação da ALU.
    uint16_t mx0 = 0;  ///< Entrada X0 do MAC.
    uint16_t mx1 = 0;  ///< Entrada X1 do MAC.
    uint16_t my0 = 0;  ///< Entrada Y0 do MAC.
    uint16_t my1 = 0;  ///< Entrada Y1 do MAC.
    uint16_t mf = 0;   ///< Realimentação do MAC.
    uint16_t si = 0;   ///< Entrada do shifter.
    int16_t se = 0;   ///< 8 bits com sinal (guardado estendido)
    uint32_t sr = 0;  ///< SR1 nos bits 16–31, SR0 nos 0–15
    int64_t mr = 0;   ///< 40 bits com sinal (bits 40–63 = extensão)
};

/**
 * @brief Estado do ADSP-2105 e operações das instruções.
 */
class AdspCore : public AdspRegs {
public:
    // --- ASTAT ----------------------------------------------------------------
    static constexpr uint16_t kAz = 0x01;  ///< ASTAT: resultado da ALU zero.
    static constexpr uint16_t kAn = 0x02;  ///< ASTAT: resultado da ALU negativo.
    static constexpr uint16_t kAv = 0x04;  ///< ASTAT: overflow da ALU.
    static constexpr uint16_t kAc = 0x08;  ///< ASTAT: carry da ALU.
    static constexpr uint16_t kAs = 0x10;  ///< ASTAT: sinal da entrada X (ABS).
    static constexpr uint16_t kAq = 0x20;  ///< ASTAT: quociente (DIVS/DIVQ).
    static constexpr uint16_t kMv = 0x40;  ///< ASTAT: overflow do MAC.
    static constexpr uint16_t kSs = 0x80;  ///< ASTAT: sinal da entrada do shifter.
    // --- MSTAT ----------------------------------------------------------------
    static constexpr uint16_t kSecReg = 0x01;   ///< MSTAT: banco secundário de registradores.
    static constexpr uint16_t kBitRev = 0x02;   ///< MSTAT: endereçamento com bits invertidos.
    static constexpr uint16_t kStickyV = 0x04;  ///< MSTAT: overflow da ALU fica preso.
    static constexpr uint16_t kArSat = 0x08;    ///< MSTAT: saturação de AR.
    static constexpr uint16_t kMMode = 0x10;    ///< MSTAT: modo inteiro do MAC (sem o deslocamento fracionário).

    /// @param mem Memórias de dados e de programa da placa.
    explicit AdspCore(IAdspMemory& mem) : mem_(mem) {}

    AdspRegs alt;           ///< banco secundário
    uint16_t astat = 0;  ///< Status aritmético.
    uint16_t mstat = 0;  ///< Status de modo.
    uint16_t imask = 0;  ///< Máscara de interrupções.
    uint16_t icntl = 0;  ///< Controle de interrupções.
    uint8_t px = 0;      ///< 8 bits baixos das transferências de 24 bits da PM.
    uint32_t cntr = 0;   ///< Contador de laço (`DO … UNTIL CE`).
    std::array<uint32_t, 8> i{};   ///< I0–I7
    std::array<int32_t, 8> m{};    ///< M0–M7 (14 bits com sinal)
    std::array<uint32_t, 8> l{};   ///< L0–L7

    /// @brief Reset do processador (registradores de controle; os de dados ficam como estão).
    void reset() {
        if (mstat & kSecReg) std::swap(static_cast<AdspRegs&>(*this), alt);
        mstat = 0;
        astat = 0;
        imask = 0;
        icntl = 0;
        cntr = 0;
        cntr_sp_ = 0;
        for (int n = 0; n < 8; ++n) {
            l[n] = 0;
            base_[n] = i[n];
        }
    }

    // --- registradores com efeitos colaterais na escrita ------------------------
    /// @return SR0 (16 bits baixos do resultado do shifter).
    uint16_t sr0() const { return static_cast<uint16_t>(sr); }
    /// @return SR1 (16 bits altos do resultado do shifter).
    uint16_t sr1() const { return static_cast<uint16_t>(sr >> 16); }
    /// Grava SR0.
    void setSr0(uint16_t v) { sr = (sr & 0xFFFF0000u) | v; }
    /// Grava SR1.
    void setSr1(uint16_t v) { sr = (sr & 0x0000FFFFu) | (static_cast<uint32_t>(v) << 16); }
    /// @return MR0 (bits 0–15 do acumulador do MAC).
    uint16_t mr0() const { return static_cast<uint16_t>(mr); }
    /// @return MR1 (bits 16–31).
    uint16_t mr1() const { return static_cast<uint16_t>(static_cast<uint64_t>(mr) >> 16); }
    /// @return MR2 (bits 32–39).
    uint16_t mr2() const { return static_cast<uint16_t>(static_cast<uint64_t>(mr) >> 32); }
    /// Grava MR0.
    void setMr0(uint16_t v) { mr = (mr & ~int64_t{0xFFFF}) | v; }
    /// Escrever MR1 estende o sinal para MR2.
    /// (Multiplicação em vez de `<<`: deslocar valor negativo à esquerda é indefinido em C++17.)
    void setMr1(uint16_t v) { mr = static_cast<int64_t>(static_cast<int16_t>(v)) * 0x10000 | (mr & 0xFFFF); }
    /// Grava MR2 (8 bits com sinal).
    void setMr2(uint16_t v) { mr = static_cast<int64_t>(static_cast<int8_t>(v)) * 0x100000000ll | (mr & 0xFFFFFFFFll); }
    /// Grava SE (expoente do shifter, 8 bits com sinal).
    void setSe(uint16_t v) { se = static_cast<int8_t>(v); }
    /// @return SE como registrador de 16 bits.
    uint16_t seReg() const { return static_cast<uint16_t>(se); }

    /// Grava MSTAT; mudar SEC_REG troca o banco de registradores de dados.
    void setMstat(uint16_t v) {
        if ((v ^ mstat) & kSecReg) std::swap(static_cast<AdspRegs&>(*this), alt);
        mstat = v & 0x7F;
    }
    /// `ENA`: liga os bits @p bits de MSTAT.
    void enable(uint16_t bits) { setMstat(mstat | bits); }
    /// `DIS`: desliga os bits @p bits de MSTAT.
    void disable(uint16_t bits) { setMstat(mstat & ~bits); }

    // --- DAGs --------------------------------------------------------------------
    /// Grava I@p n e recalcula a base do buffer circular.
    void setI(int n, uint32_t v) {
        i[n] = v & 0x3FFF;
        base_[n] = i[n] & lmask(l[n]);
    }
    /// Grava M@p n (14 bits com sinal).
    void setM(int n, uint32_t v) { m[n] = static_cast<int32_t>((v & 0x3FFF) ^ 0x2000) - 0x2000; }
    /// Grava L@p n (tamanho do buffer circular) e recalcula a base.
    void setL(int n, uint32_t v) {
        l[n] = v & 0x3FFF;
        base_[n] = i[n] & lmask(l[n]);
    }
    /// `MODIFY (In,Mm)`.
    void modify(int n, int mm) {
        uint32_t v = (i[n] + static_cast<uint32_t>(m[mm])) & 0x3FFF;
        if (v < base_[n]) v += l[n];
        else if (v >= base_[n] + l[n]) v -= l[n];
        i[n] = v;
    }
    /// `DM(In,Mm)` (leitura com pós-modificação).
    uint16_t dm(int n, int mm) {
        const uint16_t v = mem_.dmRead(dagAddr(n));
        modify(n, mm);
        return v;
    }
    /// `DM(In,Mm) = v`.
    void dmw(int n, int mm, uint16_t v) {
        mem_.dmWrite(dagAddr(n), v);
        modify(n, mm);
    }
    /// `DM(addr)` direto.
    uint16_t dmAt(uint32_t a) { return mem_.dmRead(a & 0x3FFF); }
    /// `DM(addr) = v` direto.
    void dmAtW(uint32_t a, uint16_t v) { mem_.dmWrite(a & 0x3FFF, v); }
    /// `PM(In,Mm)` como dado: devolve os bits 8–23; os bits 0–7 vão para PX.
    uint16_t pm(int n, int mm) {
        const uint32_t w = mem_.pmRead(i[n]);
        px = static_cast<uint8_t>(w);
        modify(n, mm);
        return static_cast<uint16_t>(w >> 8);
    }
    /// `PM(In,Mm) = v`: grava (v << 8) | PX.
    void pmw(int n, int mm, uint16_t v) {
        mem_.pmWrite(i[n], (static_cast<uint32_t>(v) << 8) | px);
        modify(n, mm);
    }

    // --- CNTR e laços ------------------------------------------------------------
    /// `CNTR = v` (empilha o contador atual).
    void setCntr(uint32_t v) {
        if (cntr_sp_ < static_cast<int>(cntr_stack_.size())) cntr_stack_[cntr_sp_++] = cntr;
        cntr = v & 0x3FFF;
    }
    /// `OWRCNTR = v` (sem empilhar).
    void owrCntr(uint32_t v) { cntr = v & 0x3FFF; }
    /// Fim de uma volta de `DO … UNTIL CE`: true = repete; false = sai (desempilha o contador).
    bool ce() {
        if (static_cast<int32_t>(--cntr) > 0) return true;
        if (cntr_sp_ > 0) cntr = cntr_stack_[--cntr_sp_];
        return false;
    }

    // --- condições ---------------------------------------------------------------
    /// Condição EQ (AZ).
    bool eq() const { return astat & kAz; }
    /// Condição NE.
    bool ne() const { return !eq(); }
    /// Condição LT (AN xor AV).
    bool lt() const { return ((astat & kAn) != 0) != ((astat & kAv) != 0); }
    /// Condição GE.
    bool ge() const { return !lt(); }
    /// Condição GT.
    bool gt() const { return !(lt() || eq()); }
    /// Condição LE.
    bool le() const { return lt() || eq(); }
    /// Condição AC (carry).
    bool acFlag() const { return astat & kAc; }
    /// Condição NEG (AS).
    bool neg() const { return astat & kAs; }
    /// Condição MV (overflow do MAC).
    bool mv() const { return astat & kMv; }

    // --- ALU (resultado bruto de 32 bits; use toAr/toAf) ---------------------------
    int32_t aluPass(uint16_t y) { return nz(y); }                       ///< `Y` (e `X` = X + 0)
    /// `Y + 1`, com AV/AC.
    int32_t aluInc(uint16_t y) {
        const int32_t r = nz(static_cast<int32_t>(y) + 1);
        if (y == 0x7FFF) astat |= kAv;
        else if (y == 0xFFFF) astat |= kAc;
        return r;
    }
    /// `Y − 1`, com AV/AC.
    int32_t aluDec(uint16_t y) {
        const int32_t r = nz(static_cast<int32_t>(y) - 1);
        if (y == 0x8000) astat |= kAv;
        else if (y == 0x0000) astat |= kAc;
        return r;
    }
    /// `−Y`, com AV/AC.
    int32_t aluNeg(uint16_t y) {
        const int32_t r = nz(-static_cast<int32_t>(y));
        if (y == 0x8000) astat |= kAv;
        if (y == 0x0000) astat |= kAc;
        return r;
    }
    /// `NOT X`.
    int32_t aluNot(uint16_t v) { return nz(v ^ 0xFFFF); }
    /// `X AND Y`.
    int32_t aluAnd(uint16_t x, uint16_t y) { return nz(x & y); }
    /// `X OR Y`.
    int32_t aluOr(uint16_t x, uint16_t y) { return nz(x | y); }
    /// `X XOR Y`.
    int32_t aluXor(uint16_t x, uint16_t y) { return nz(x ^ y); }
    /// `X + Y`.
    int32_t aluAdd(uint16_t x, uint16_t y) {
        const int32_t r = static_cast<int32_t>(x) + y;
        flagsNzv(x, y, r);
        astat |= (static_cast<uint32_t>(r) >> 13) & kAc;
        return r;
    }
    /// `A - B` (X − Y ou Y − X, conforme a instrução).
    int32_t aluSub(uint16_t a, uint16_t b) {
        const int32_t r = static_cast<int32_t>(a) - b;
        flagsNzv(a, b, r);
        astat |= (~static_cast<uint32_t>(r) >> 13) & kAc;
        return r;
    }
    /// `ABS X`.
    int32_t aluAbs(uint16_t x) {
        const int32_t r = (x & 0x8000) ? -static_cast<int32_t>(x) : x;
        astat &= clearMask() & ~kAs;
        if (x == 0) astat |= kAz;
        if (x == 0x8000) astat |= kAn | kAv;
        if (x & 0x8000) astat |= kAs;
        return r;
    }
    /// Resultado para AR (satura com AR_SAT e AV).
    void toAr(int32_t r) {
        if ((mstat & kArSat) && (astat & kAv)) r = (astat & kAc) ? -32768 : 32767;
        ar = static_cast<uint16_t>(r);
    }
    /// Resultado para AF (sem saturação).
    void toAf(int32_t r) { af = static_cast<uint16_t>(r); }

    /// `DIVS Y, X` (Y = AF, X = AX0 aqui).
    void divs(uint16_t y, uint16_t x) {
        const uint32_t t = static_cast<uint32_t>(x) ^ y;
        astat = static_cast<uint16_t>((astat & ~kAq) | ((t >> 10) & kAq));
        af = static_cast<uint16_t>((y << 1) | (ay0 >> 15));
        ay0 = static_cast<uint16_t>((ay0 << 1) | ((t >> 15) & 1));
    }
    /// `DIVQ X`.
    void divq(uint16_t x) {
        const int32_t r = (astat & kAq) ? static_cast<int32_t>(af) + x : static_cast<int32_t>(af) - x;
        const uint32_t t = static_cast<uint32_t>(r) ^ x;
        astat = static_cast<uint16_t>((astat & ~kAq) | ((t >> 10) & kAq));
        af = static_cast<uint16_t>((r << 1) | (ay0 >> 15));
        ay0 = static_cast<uint16_t>((ay0 << 1) | ((~t >> 15) & 1));
    }

    // --- MAC -----------------------------------------------------------------------
    /// Formato dos operandos do MAC: com sinal (S) ou sem sinal (U), X e Y.
    enum class Mul { SS, SU, US, UU };
    /// Produto já deslocado conforme o modo (fracionário: << 1).
    int64_t product(uint16_t x, uint16_t y, Mul mode) const {
        const int64_t xv = (mode == Mul::SS || mode == Mul::SU) ? static_cast<int16_t>(x) : static_cast<int64_t>(x);
        const int64_t yv = (mode == Mul::SS || mode == Mul::US) ? static_cast<int16_t>(y) : static_cast<int64_t>(y);
        const int64_t p = xv * yv;
        return (mstat & kMMode) ? p : p * 2;
    }
    /// Grava MR (40 bits) e atualiza MV.
    void mrSet(int64_t r) {
        mr = signExtend40(r);
        const int32_t top = static_cast<int32_t>((static_cast<uint64_t>(mr) >> 31) & 0x1FF);
        astat &= ~kMv;
        if (top != 0 && top != 0x1FF) astat |= kMv;
    }
    /// `MR = X * Y (mode)`.
    void mul(uint16_t x, uint16_t y, Mul mode) { mrSet(product(x, y, mode)); }
    /// `MR = MR + X * Y (mode)`.
    void mac(uint16_t x, uint16_t y, Mul mode) { mrSet(mr + product(x, y, mode)); }
    /// `MR = MR - X * Y (mode)`.
    void msub(uint16_t x, uint16_t y, Mul mode) { mrSet(mr - product(x, y, mode)); }
    /// `MR = X * Y (RND)` (SS).
    void mulRnd(uint16_t x, uint16_t y) { mrSet(round(product(x, y, Mul::SS))); }
    /// `MR = MR + X * Y (RND)`.
    void macRnd(uint16_t x, uint16_t y) { mrSet(round(mr + product(x, y, Mul::SS))); }
    /// `MR = MR - X * Y (RND)`.
    void msubRnd(uint16_t x, uint16_t y) { mrSet(round(mr - product(x, y, Mul::SS))); }
    /// `IF MV SAT MR`.
    void satMr() {
        if (!(astat & kMv)) return;
        mr = (mr & (int64_t{1} << 39)) ? -(int64_t{1} << 31) : (int64_t{1} << 31) - 1;
    }

    // --- deslocador ------------------------------------------------------------------
    /// Deslocamento aritmético de @p v por @p n bits (0–31), escrito sem `>>` sobre negativo: em C++17 o
    /// resultado desse `>>` depende do compilador; aqui é o mesmo em todos.
    static constexpr int32_t sra(int32_t v, int n) { return v < 0 ? ~(~v >> n) : v >> n; }
    /// `SR = [SR OR] LSHIFT x BY n (HI|LO)`.
    void lshift(uint16_t x, int n, bool hi, bool orSr = false) {
        const uint32_t v = hi ? static_cast<uint32_t>(x) << 16 : x;
        uint32_t r;
        if (n > 0) r = n < 32 ? v << n : 0;
        else r = n > -32 ? v >> -n : 0;
        sr = orSr ? (sr | r) : r;
    }
    /// `SR = [SR OR] ASHIFT x BY n (HI|LO)`.
    void ashift(uint16_t x, int n, bool hi, bool orSr = false) {
        const int32_t v = hi ? static_cast<int32_t>(static_cast<int16_t>(x)) * 0x10000 : static_cast<int16_t>(x);
        uint32_t r;
        if (n > 0) r = n < 32 ? static_cast<uint32_t>(v) << n : 0;
        else r = static_cast<uint32_t>(sra(v, n > -32 ? -n : 31));
        sr = orSr ? (sr | r) : r;
    }
    /// `SE = EXP x (HI)`.
    void expHi(uint16_t x) {
        const int16_t v = static_cast<int16_t>(x);
        int lead = 0;
        if (v < 0) {
            astat |= kSs;
            for (int b = 15; b >= 0 && ((x >> b) & 1); --b) ++lead;
        } else {
            astat &= ~kSs;
            for (int b = 15; b >= 0 && !((x >> b) & 1); --b) ++lead;
        }
        se = static_cast<int16_t>(-(lead - 1));
    }

private:
    /// Máscara da base do buffer circular para o tamanho @p len (como o MAME).
    static uint32_t lmask(uint32_t len) {
        if (len > 0x2000) return 0x0000;
        uint32_t mask = 0x3FFF;
        for (uint32_t lim = 1; lim < 0x2000 && len > lim; lim <<= 1) mask = (mask << 1) & 0x3FFF;
        return mask;
    }
    /// Endereço efetivo de I@p n (bits invertidos em I0–I3 com BIT_REV).
    uint32_t dagAddr(int n) const {
        const uint32_t a = i[n];
        if (n >= 4 || !(mstat & kBitRev)) return a;
        uint32_t r = 0;
        for (int b = 0; b < 14; ++b) r |= ((a >> b) & 1u) << (13 - b);
        return r;
    }
    /// Bits de ASTAT limpos por uma operação da ALU (AV fica preso com AV_LATCH).
    uint16_t clearMask() const {
        return static_cast<uint16_t>((mstat & kStickyV) ? ~(kAc | kAn | kAz) : ~(kAc | kAv | kAn | kAz));
    }
    /// Atualiza AZ e AN para o resultado @p r. @return @p r.
    int32_t nz(int32_t r) {
        astat &= clearMask();
        if ((r & 0xFFFF) == 0) astat |= kAz;
        astat |= (static_cast<uint32_t>(r) >> 14) & kAn;
        return r;
    }
    /// Atualiza AZ, AN e AV de uma soma/subtração (@p s, @p d → @p r).
    void flagsNzv(int32_t s, int32_t d, int32_t r) {
        nz(r);
        astat |= (static_cast<uint32_t>(s ^ d ^ r ^ sra(r, 1)) >> 13) & kAv;
    }
    /// Estende o sinal de um valor de 40 bits.
    static int64_t signExtend40(int64_t v) {
        v &= (int64_t{1} << 40) - 1;
        return (v & (int64_t{1} << 39)) ? v - (int64_t{1} << 40) : v;
    }
    /// Arredondamento não tendencioso: soma 0x8000; se MR0 era 0x8000, zera o bit 16.
    static int64_t round(int64_t v) {
        v += 0x8000;
        if ((v & 0xFFFF) == 0) v &= ~int64_t{0x10000};
        return v;
    }

    IAdspMemory& mem_;                      ///< Memórias de dados e de programa.
    std::array<uint32_t, 8> base_{};        ///< Base do buffer circular de cada I (I & máscara de L).
    std::array<uint32_t, 4> cntr_stack_{};  ///< Pilha do contador de laço.
    int cntr_sp_ = 0;                       ///< Topo da pilha do contador.
};

} // namespace rea::core::audio::dcs
