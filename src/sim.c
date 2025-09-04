#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "shell.h"

// Obtiene el valor del registro considerando XZR (x31)
int64_t get_reg_value(int reg_num) {
    return (reg_num == 31) ? 0 : CURRENT_STATE.REGS[reg_num];
}

// Establece el valor del registro considerando XZR (x31)
void set_reg_value(int reg_num, int64_t value) {
    if (reg_num != 31) {
        NEXT_STATE.REGS[reg_num] = value;
    }
}

// Actualiza flags según el resultado
void update_flags(int64_t result) {
    NEXT_STATE.FLAG_Z = (result == 0) ? 1 : 0;
    NEXT_STATE.FLAG_N = (result < 0) ? 1 : 0;
}

void update_flags_add(uint64_t a, uint64_t b, uint64_t result) {
    NEXT_STATE.FLAG_Z = (result == 0) ? 1 : 0;
    NEXT_STATE.FLAG_N = (result < 0) ? 1 : 0;
    NEXT_STATE.FLAG_C = (result < a);
    NEXT_STATE.FLAG_V = (((a ^ result) & (b ^ result)) >> 63) & 1;
}

void update_flags_sub(uint64_t a, uint64_t b, uint64_t result) {
    NEXT_STATE.FLAG_Z = (result == 0) ? 1 : 0;
    NEXT_STATE.FLAG_N = (result < 0) ? 1 : 0;
    NEXT_STATE.FLAG_C = (a >= b);
    NEXT_STATE.FLAG_V = (((a ^ b) & (a ^ result)) >> 63) & 1;
}


// Extrae y extiende el signo del 9-bit immediate
int32_t extract_imm9(uint32_t instruction) {
    int32_t imm9 = (instruction >> 12) & 0x1FF;
    if (imm9 & 0x100) {
        imm9 |= 0xFFFFFE00;
    }
    return imm9;
}

// Extrae y extiende el signo del 19-bit immediate for branches
int32_t extract_imm19(uint32_t instruction) {
    int32_t imm19 = (instruction >> 5) & 0x7FFFF;
    if (imm19 & (1 << 18)) {
        imm19 |= ~((1 << 19) - 1);
    }
    return imm19 << 2; // Multiplica por 4 para alineación
}

void process_instruction()
{
    uint32_t instruction = mem_read_32(CURRENT_STATE.PC);
    uint32_t opcode = (instruction >> 21) & 0x7FF;
    uint32_t opcode_high = (instruction >> 24) & 0xFF;

    uint32_t Rd = instruction & 0x1F;
    uint32_t Rn = (instruction >> 5) & 0x1F;
    uint32_t Rm = (instruction >> 16) & 0x1F;

    // Caso especial para instrucciones B.Cond (comienzan con 0x54)
    if (opcode_high == 0x54) {
        int flag_n = CURRENT_STATE.FLAG_N;
        int flag_z = CURRENT_STATE.FLAG_Z;
        uint8_t cond = instruction & 0xF;
        int32_t imm19 = extract_imm19(instruction);

        uint64_t new_address = CURRENT_STATE.PC + imm19;

        int should_branch = 0;
        switch (cond) {
            case 0x0: // BEQ
                should_branch = (flag_z == 1);
                break;
            case 0x1: // BNE
                should_branch = (flag_z == 0);
                break;
            case 0xC: // BGT
                should_branch = (flag_z == 0 && flag_n == 0);
                break;
            case 0xB: // BLT
                should_branch = (flag_n == 1);
                break;
            case 0xA: // BGE
                should_branch = (flag_n == 0);
                break;
            case 0xD: // BLE
                should_branch = (flag_z == 1 || flag_n == 1);
                break;
            default:
                printf("B.Cond: condition desconocida 0x%X\n", cond);
                NEXT_STATE.PC = CURRENT_STATE.PC + 4;
                return;
        }

        if (should_branch) {
            NEXT_STATE.PC = new_address;
        } else {
            NEXT_STATE.PC = CURRENT_STATE.PC + 4;
        }

        return;
    } else {
        // Switch regular para el resto de instrucciones
        switch (opcode) {
            case 0x6A2:  // HLT
                RUN_BIT = FALSE;  // Detener simulación
                break;

            case 0x558: {  // ADDS Register

                int64_t reg_Xn = get_reg_value(Rn);  // Manejo de XZR
                int64_t reg_Xm = get_reg_value(Rm);

                int64_t result = reg_Xn + reg_Xm;
                set_reg_value(Rd, result);  // XZR permanece en 0

                update_flags_add(reg_Xn, reg_Xm, result);
                break;
            }

            case 0x758: {  // SUBS Register (también implementa CMP Register cuando Rd=31/XZR)

                int64_t reg_Xn = get_reg_value(Rn);
                int64_t reg_Xm = get_reg_value(Rm);
                int64_t result = reg_Xn - reg_Xm;

                set_reg_value(Rd, result);

                // Actualizar FLAGS
                update_flags_sub(reg_Xn, reg_Xm, result);
                break;
            }

            case 0x588: {  // ADDS Immediate

                uint32_t imm12 = (instruction >> 10) & 0xFFF;

                int64_t reg_Xn = get_reg_value(Rn);
                int64_t imm = imm12;
                int64_t result = reg_Xn + imm;

                set_reg_value(Rd, result);

                update_flags_add(reg_Xn, imm, result);
                break;
            }

            case 0x788: {  // SUBS Immediate (también implementa CMP Immediate cuando Rd=31/XZR)

                uint32_t imm12 = (instruction >> 10) & 0xFFF;
                uint32_t shift = (instruction >> 22) & 0x3;

                // Aplicar shift si es necesario (01 = LSL #12)
                if (shift == 1) {
                    imm12 <<= 12;
                }

                int64_t reg_Xn = get_reg_value(Rn);
                int64_t result = reg_Xn - imm12;

                set_reg_value(Rd, result);

                // Actualizar FLAGS
                update_flags_sub(reg_Xn, imm12, result);
                break;
            }
            case 0x5d0: { // ADCS
            
                uint64_t reg_Xn = get_reg_value(Rn);
                uint64_t reg_Xm = get_reg_value(Rm);
                uint64_t carry = CURRENT_STATE.FLAG_C;
                uint64_t result = reg_Xn + reg_Xm + carry;

                set_reg_value(Rd, result);

                update_flags_add(reg_Xn, reg_Xm + carry, result);
                break;
            }
            case 0x750: {  // ANDS (Shifted Register)

                int64_t reg_Xn = get_reg_value(Rn);
                int64_t reg_Xm = get_reg_value(Rm);
                int64_t result = reg_Xn & reg_Xm;

                set_reg_value(Rd, result);

                // Actualizar FLAGS
                update_flags(result);
                break;
            }
            case 0x650: {  // EOR (Shifted Register)

                int64_t reg_Xn = get_reg_value(Rn);
                int64_t reg_Xm = get_reg_value(Rm);
                int64_t result = reg_Xn ^ reg_Xm;

                set_reg_value(Rd, result);
                break;
            }
            case 0x550: {  // ORR (Shifted Register)

                int64_t reg_Xn = get_reg_value(Rn);
                int64_t reg_Xm = get_reg_value(Rm);
                int64_t result = reg_Xn | reg_Xm;

                set_reg_value(Rd, result);
                break;
            }
            case 0x0A0: {  // B (Branch)
                int32_t imm26 = (instruction & 0x03FFFFFF);

                // Sign-extend el immediate a 64 bits y multiplicar por 4 (instrucciones de 4 bytes)
                int64_t offset = ((int64_t)((int32_t)(imm26 << 6)) >> 6) * 4;

                NEXT_STATE.PC = CURRENT_STATE.PC + offset;
                return;
            }
            case 0x6B0: {  // BR (Branch Register)

                NEXT_STATE.PC = CURRENT_STATE.REGS[Rn];
                return;
            }
            case 0x694: {  // MOVZ

                uint32_t imm16 = (instruction >> 5) & 0xFFFF;  // Extraer immediate de 16 bits
                uint32_t hw = (instruction >> 21) & 0x3;  // Extraer hw (shift amount)

                // De acuerdo a la consigna, solo implementamos para hw = 0
                int64_t result = imm16;

                // Si hw no es 0,  mostramos una advertencia pero continuamos con hw = 0
                if (hw != 0) {
                    printf("MOVZ: Advertencia - hw != 0 no implementado, usando hw = 0\n");
                }

                set_reg_value(Rd, result);
                break;
            }

            case 0x69A: {  // LSL (Immediate), e.g., lsl X4, X3, 4

                    uint32_t immr = (instruction >> 10) & 0x3F;

                    uint64_t shift = 63 - immr;
                    uint64_t src = CURRENT_STATE.REGS[Rn];
                    uint64_t result = src << shift;
                    set_reg_value(Rd, result);

                    update_flags(result);


                    break;
                }

            case 0x69B:// LSR
                {

                    uint32_t imms = (instruction >> 16) & 0x3F;  // imms

                    uint64_t shift = imms;
                    uint64_t src = CURRENT_STATE.REGS[Rn];
                    uint64_t result = src >> shift;
                    set_reg_value(Rd, result);
                    update_flags(result);


                    break;
                }
            case 0x7c0: // STUR
            {
                int32_t imm9 = extract_imm9(instruction);

                uint32_t address = CURRENT_STATE.REGS[Rn] + imm9;
                mem_write_32(address, CURRENT_STATE.REGS[Rd]);
            }
            break;
            case 0x1c0: // STURB
                {

                    int32_t imm9 = extract_imm9(instruction);

                uint32_t address = CURRENT_STATE.REGS[Rn] + imm9;
                uint32_t value = mem_read_32(address);
                value = (value & 0xFFFFFF00) | (CURRENT_STATE.REGS[Rd] & 0xFF);
                mem_write_32(address, value);
                }
                break;
            case 0x3E1: // STURH
                {

                    int32_t imm9 = extract_imm9(instruction);

                    uint32_t address = CURRENT_STATE.REGS[Rn] + imm9;
                    uint32_t value = mem_read_32(address);
                    value = (value & 0xFFFF0000) | (CURRENT_STATE.REGS[Rd] & 0xFFFF);
                    mem_write_32(address, value);
                }
                break;
            case 0x7c2: // LDUR
                {
                    int64_t reg_Xn = CURRENT_STATE.REGS[Rn];
                    int32_t offset = extract_imm9(instruction);

                    uint64_t address = reg_Xn + offset;

                    // Leer dos palabras de 32 bits y combinarlas para formar 64 bits
                    uint32_t low_word = mem_read_32(address);
                    uint32_t high_word = mem_read_32(address + 4);

                    // Combinar en un valor de 64 bits (little-endian)
                    uint64_t value = ((uint64_t)high_word << 32) | low_word;

                    set_reg_value(Rd, value);
                    break;
                }
            case 0x1c2: // LDURB
                {
                    int64_t reg_Xn = CURRENT_STATE.REGS[Rn];
                    int32_t offset = extract_imm9(instruction);

                    uint64_t address = reg_Xn + offset;

                    // Leer 32 bits y extraer solo el primer byte
                    uint32_t word = mem_read_32(address);
                    uint8_t byte = word & 0xFF;

                    // Extender con ceros (56 bits de ceros + 8 bits de datos)
                    uint64_t value = (uint64_t)byte;

                    set_reg_value(Rd, value);
                    break;
                }
            case 0x3c2: // LDURH
                {
                    int64_t reg_Xn = CURRENT_STATE.REGS[Rn];
                    int32_t offset = extract_imm9(instruction);

                    uint64_t address = reg_Xn + offset;

                    // Leer 32 bits y extraer solo los primeros 16 bits
                    uint32_t word = mem_read_32(address);
                    uint16_t halfword = word & 0xFFFF;

                    // Extender con ceros (48 bits de ceros + 16 bits de datos)
                    uint64_t value = (uint64_t)halfword;

                    set_reg_value(Rd, value);
                    break;
                }

            // ADD (Extended Register) - Similar a ADDS pero sin actualizar flags
            case 0x458: {  // ADD Register
                int64_t reg_Xn = get_reg_value(Rn);
                int64_t reg_Xm = get_reg_value(Rm);

                int64_t result = reg_Xn + reg_Xm;
                set_reg_value(Rd, result);
                break;
            }

            // ADD (Immediate) - Similar a ADDS pero sin actualizar flags
            case 0x488: {  // ADD Immediate
                uint32_t imm12 = (instruction >> 10) & 0xFFF;
                uint32_t shift = (instruction >> 22) & 0x3;

                int64_t reg_Xn = get_reg_value(Rn);
                int64_t imm = imm12;

                // Aplicar shift si es necesario (01 = LSL #12)
                if (shift == 1) {
                    imm = imm12 << 12;
                }

                int64_t result = reg_Xn + imm;
                set_reg_value(Rd, result);
                break;
            }

            // MUL - Multiplicación básica
            case 0x4D8: {  // MUL
                int64_t reg_Xn = get_reg_value(Rn);
                int64_t reg_Xm = get_reg_value(Rm);

                int64_t result = reg_Xn * reg_Xm;
                set_reg_value(Rd, result);
                break;
            }

            // CBZ - Compare and Branch if Zero
            case 0x5A0: {  // CBZ
                uint64_t reg_value = CURRENT_STATE.REGS[Rd];
                int32_t imm19 = extract_imm19(instruction);

                if (reg_value == 0) {
                    NEXT_STATE.PC = CURRENT_STATE.PC + imm19;
                } else {
                    NEXT_STATE.PC = CURRENT_STATE.PC + 4;
                }
                return;
            }

            // CBNZ - Compare and Branch if Not Zero
            case 0x5A8: {  // CBNZ
                uint64_t reg_value = CURRENT_STATE.REGS[Rd];
                int32_t imm19 = extract_imm19(instruction);

                if (reg_value != 0) {
                    NEXT_STATE.PC = CURRENT_STATE.PC + imm19;
                } else {
                    NEXT_STATE.PC = CURRENT_STATE.PC + 4;
                }
                return;
            }

            default:
                printf("Instrucción desconocida: %x\n", opcode);
                break;
        }
    }
    
    NEXT_STATE.PC = CURRENT_STATE.PC + 4;
    
}