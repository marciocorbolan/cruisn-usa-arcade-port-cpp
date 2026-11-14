/**
 * @file main_loop.cpp
 * @brief Tradução linha a linha do loop principal (ROM `0x4BAF`–`0x4C32`).
 */
#include "core/main/main_loop.hpp"

namespace rea::core::mainloop {

using namespace main_vars;

namespace {
/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
int32_t s32(uint32_t v) { return static_cast<int32_t>(v); }
} // namespace

void MainLoop::init(Tms32031State& cpu) {
    cpu.dp = 0;
    // 04BAF  AR2 = 0x21; CALL $99E9; CMPI 0,R0; BGT $4BB9
    cpu.ar[2] = kItem21;
    call(0x99E9, cpu);
    if (s32(cpu.r[0]) <= 0) {
        // 04BB3  CALL $9A99; item 0x21 = item 0x27 (com checksum)
        call(0x9A99, cpu);
        cpu.ar[2] = kItem27;
        call(0x99E9, cpu);
        cpu.r[2] = cpu.r[0];
        cpu.ar[2] = kItem21;
        call(0x99EE, cpu);
    }
    // 04BB9  [C92F] = 1; [C935] = [C934] = 0; CALL $4F08; CALL $7989
    cpu.r[0] = 1;
    wr(kFrameDelta, cpu.r[0]);
    cpu.r[0] = 0;
    wr(kVarC935, cpu.r[0]);
    wr(kVarC934, cpu.r[0]);
    call(0x4F08, cpu);
    call(0x7989, cpu);
}

bool MainLoop::dmaBusy(Tms32031State& cpu, uint32_t mask) {
    // LDIU $98,DP; LDI @($0082),R0; AND mask,R0; BNE (volta); SUBI DP,DP
    cpu.dp = 0x98;
    cpu.r[0] = rd(kVideoStatus) & mask;
    if (cpu.r[0] != 0) return true;
    cpu.dp = 0;
    return false;
}

void MainLoop::afterDma(Tms32031State& cpu) {
    // 04BCA  CALL $4EE3; $4F27; $A323; $4F27
    call(0x4EE3, cpu);
    call(0x4F27, cpu);
    call(0xA323, cpu);
    call(0x4F27, cpu);
}

Point MainLoop::body(Tms32031State& cpu) {
    // 04BD6  CALL $7F5D; $7989; $794C
    call(0x7F5D, cpu);
    call(0x7989, cpu);
    call(0x794C, cpu);
    // 04BD9  R0 = [C930]; R1 = 0; [C930] = 0; [C92F] = R0 (quadros desta iteração)
    cpu.r[0] = rd(kFrameCounter);
    cpu.r[1] = 0;
    wr(kFrameCounter, cpu.r[1]);
    wr(kFrameDelta, cpu.r[0]);
    // 04BDD  CALL $4F27; STF R0,@($C936); CALL $4F08
    call(0x4F27, cpu);
    wr(kIterationStamp, cpu.r[0]);
    call(0x4F08, cpu);
    // 04BE0  se [C8C5] & 0x40: CALL $822B
    cpu.r[4] = rd(kGameMode);
    if (cpu.r[4] & 0x0040u) call(0x822B, cpu);
    call(0x4F27, cpu);
    // 04BE4  se [C98B] != 0: CALL $A6F8; CALL $0064
    cpu.r[0] = rd(kVarC98B);
    if (cpu.r[0] != 0) call(0xA6F8, cpu);
    call(0x0064, cpu);
    call(0x4F27, cpu);
    // 04BE8  se [C8C5] & 0x100 e modo ∈ {4, 5}: CALL $9CFF
    cpu.r[4] = rd(kGameMode);
    if (cpu.r[4] & 0x0100u) {
        cpu.r[4] &= 0x000Fu;
        if (cpu.r[4] == 4 || cpu.r[4] == 5) call(0x9CFF, cpu);
    }
    call(0x4F27, cpu);
    // 04BF2  modo == 2: CALL $745E; CALL $7594; CALL $4F27
    cpu.r[4] = rd(kGameMode) & 0x000Fu;
    if (cpu.r[4] == 2) call(0x745E, cpu);
    call(0x7594, cpu);
    call(0x4F27, cpu);
    // 04BF8  LDI @($C8C5),R0 — mas o teste usa R4 (que ainda tem o modo de
    //        04BF2, salvo se 0x745E/0x7594 o alterarem). Peculiaridade do original.
    cpu.r[0] = rd(kGameMode);
    cpu.r[4] &= 0x000Fu;
    if (cpu.r[4] != 4) call(0x4834, cpu);
    // 04BFD  CALL $4E1B; $4DE6; $21D0
    call(0x4E1B, cpu);
    call(0x4DE6, cpu);
    call(0x21D0, cpu);
    // 04C00  [0x8099E0] = 0 — zera o watchdog de software contado pela INT0.
    cpu.r[0] = 0;
    cpu.dp = 0x80;
    wr(kSoftWatchdog, cpu.r[0]);
    cpu.dp = 0;
    // 04C04  CALL $A872; $798F; $774A; $4F27
    call(0xA872, cpu);
    if (takeJump(cpu)) return Point::SlaveDma0;
    call(0x798F, cpu);
    call(0x774A, cpu);
    call(0x4F27, cpu);
    ++iterations_;
    // 04C08  só segue para a ressincronização se: modo 2, Slave, [C8DB] != [C8DC] e [C8DB] == −2
    cpu.r[0] = rd(kGameMode) & 0x000Fu;
    if (cpu.r[0] != 2) return Point::LoopTop;
    cpu.r[0] = rd(kDipSwitches);
    if ((cpu.r[0] & 0x0001u) == 0) return Point::LoopTop;
    cpu.ar[2] = rd(kLinkStateA);
    if (cpu.ar[2] == rd(kLinkStateB)) return Point::LoopTop;
    if (s32(cpu.ar[2]) != -2) return Point::LoopTop;
    // 04C14  [C8DB] = [C8E8] = AR2; CALL $9166; SP = [0x63] (descarta a pilha);
    //        AR2 = [C8E8]; CALL $9503; BU $4C1B
    wr(kLinkStateA, cpu.ar[2]);
    wr(kLinkSave, cpu.ar[2]);
    call(0x9166, cpu);
    cpu.sp = rd(kInitialSpPtr);
    cpu.ar[2] = rd(kLinkSave);
    call(0x9503, cpu);
    return Point::SlaveDma0;
}

bool MainLoop::takeJump(Tms32031State& cpu) {
    if (cpu.jump == 0) return false;
    // Port: só existe o destino 0x4C1B (0x1E17/0x1E1C e o próprio loop em 0x4C1A).
    cpu.jump = 0;
    return true;
}

bool MainLoop::afterSlaveFlip(Tms32031State& cpu) {
    // 04C2C  CALL $798F; $7F5D; $7989; $794C; $A872; $798F; BR $4BD6
    call(0x798F, cpu);
    call(0x7F5D, cpu);
    call(0x7989, cpu);
    call(0x794C, cpu);
    call(0xA872, cpu);
    if (takeJump(cpu)) return false;
    call(0x798F, cpu);
    return true;
}

Point MainLoop::step(Tms32031State& cpu) {
    bool ran_body = false;
    for (;;) {
        switch (pc_) {
        case Point::Init:
            init(cpu);
            pc_ = Point::LoopTop;
            break;
        case Point::LoopTop:
            // Como no hardware, segue direto para a próxima espera (DMA, limitador ou
            // troca de página pedida pela 0x7187); o limite é um corpo por chamada.
            pc_ = Point::WaitDma0;
            break;
        case Point::WaitDma0:           // 04BC0
            if (dmaBusy(cpu, 0x1)) return pc_;
            pc_ = Point::WaitDma1;
            break;
        case Point::WaitDma1:           // 04BC5
            if (dmaBusy(cpu, 0x2)) return pc_;
            afterDma(cpu);
            pc_ = Point::WaitFrames;
            break;
        case Point::WaitFrames:         // 04BCE  LDI @($C930),R0; CMPI @($C931),R0; BLT
            cpu.r[0] = rd(kFrameCounter);
            if (s32(cpu.r[0]) < s32(rd(kFrameLimit))) return pc_;
            // 04BD1  CALL $4F27; $7187; $4F27
            call(0x4F27, cpu);
            call(0x7187, cpu);
            call(0x4F27, cpu);
            pc_ = Point::WaitFlip;
            break;
        case Point::WaitFlip:           // 04BD4  LDI @($C93F),R0; BNE
            cpu.r[0] = rd(kFlipRequest);
            if (cpu.r[0] != 0) return pc_;
            pc_ = Point::Body;
            break;
        case Point::Body:
            if (ran_body) return pc_;
            ran_body = true;
            pc_ = body(cpu);
            break;
        case Point::SlaveDma0:          // 04C1B
            if (dmaBusy(cpu, 0x1)) return pc_;
            pc_ = Point::SlaveDma1;
            break;
        case Point::SlaveDma1:          // 04C20
            if (dmaBusy(cpu, 0x2)) return pc_;
            // 04C25  CALL $4EE3; [C93F] = 1 (pede troca de página); [C940] = 0
            call(0x4EE3, cpu);
            cpu.r[0] = 1;
            wr(kFlipRequest, cpu.r[0]);
            cpu.r[0] = 0;
            wr(kVarC940, cpu.r[0]);
            pc_ = Point::SlaveWaitFlip;
            break;
        case Point::SlaveWaitFlip:      // 04C2A
            cpu.r[0] = rd(kFlipRequest);
            if (cpu.r[0] != 0) return pc_;
            pc_ = afterSlaveFlip(cpu) ? Point::Body : Point::SlaveDma0;
            break;
        }
    }
}

} // namespace rea::core::mainloop
