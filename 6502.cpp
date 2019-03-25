// Most of this is by Neil Bradley circa 1998?
// The hacked parts are by James.

#include <stdio.h>
#include "global.h"
#include "6502.h"

#ifndef TRUE
#define TRUE	1
#define FALSE	0
#endif

// These are required to trap 6502 access to 5200 HW registers
extern uint8 get6502memory(uint16 addr);
extern void put6502memory(uint16 addr, uint8 value);

/* flags register 'P' = NVRBDIZC */

// Address mask. Atari Asteroids/Deluxe use 0x7fff -
// but use 0xffff for full 16 bit decode
static const uint16 addrmask = 0xFFFF;

static uint16 GETWORD(uint16 addr)
{
	// Ignore address mask for 5200
	//return (get6502memory(addr & addrmask) | (get6502memory(addr & addrmask) << 8));
	return (get6502memory(addr) | (get6502memory(addr + 1) << 8));
}


// Contructor
C6502::C6502()
	: A(0),
		X(0),
		Y(0),
		P(G_FLAG),		//0x20;
		S(0xFF),
		PC(0),
		m_nmiBusy(FALSE),
		m_irqBusy(FALSE),
		m_irqPending(FALSE)
{
	// Set up function pointers
	InitialiseInstructions();

	// DO NOT Reset here as mapper is not set up yet

	// Set up state
	opcode = 0;
	clockTicks6502 = 0;
}

// Reset CPU
void C6502::Reset()
{
		m_nmiBusy = FALSE;
		m_irqBusy = FALSE;
		m_irqPending = FALSE;

		A = X = Y = P = 0;
		P |= G_FLAG;		//0x20;
		S = 0xFF;
		PC = GETWORD(0xFFFC);
		//fprintf(logfile, "Reset vector: %4X    addrmask %4X\n", PC, addrmask);
}

// Non maskerable interrupt
void C6502::DoNMI()
{
		put6502memory(0x0100 + S--, (uint8)(PC >> 8));
		put6502memory(0x0100 + S--, (uint8)(PC & 0xff));
		put6502memory(0x0100 + S--, P);
		P |= I_FLAG; //0x04;
		PC = GETWORD(0xFFFA);
		m_nmiBusy = TRUE;
}

// Maskerable Interrupt
void C6502::DoIRQ()
{
#ifdef DEBUG
    DebugPrint("IRQ !\n");
#endif

		// only do if not busy with NMI
		if(m_nmiBusy) {
				m_irqPending = TRUE;
#ifdef _DEBUG
			//DebugPrint("IRQ deferred (NMI busy) vcount = %d\n", vcount);
			DebugPrint("IRQ deferred (NMI busy) vcount = %d\n", get6502memory(0xD40B) * 2);
#endif
		} else {
#ifdef _DEBUG
		//DebugPrint("IRQ triggered. IRQST = %2X, vcount = %d\n", irqst, vcount);
		DebugPrint("IRQ triggered. IRQST = %2X, vcount = %d\n", get6502memory(0xE80E), get6502memory(0xD40B) * 2);
#endif
		put6502memory(0x0100 + S--, (uint8)(PC >> 8));
		put6502memory(0x0100 + S--, (uint8)(PC & 0xff));
		put6502memory(0x0100 + S--, P);
		P |= I_FLAG; //0x04;
		PC = GETWORD(0xFFFE);
		m_irqBusy = TRUE;
		}
}

// NOTE: exec6520() etc moved to C5200::Run() etc


/// Execute the next instruction
/// @return				Number of CPU clock cycles used
int C6502::ExecuteInstruction()
{
	// fetch instruction
	opcode = memory5200[PC++];

	// calculate clock cycles
	// bug fix: this goes before instruction[] 'cos
	// instruction[] may alter clockTicks6502
	clockTicks6502 = ticks[opcode];

	// execute instruction
	(this->*instruction[opcode])();

	return clockTicks6502;
}


///////////////////////////////////////////////////////////////////////////////
/// Adressing modes
///////////////////////////////////////////////////////////////////////////////

/* Implied */
void C6502::implied6502()
{
}

/* #Immediate */
void C6502::immediate6502()
{
		savepc = PC++;
}

/* ABS */
void C6502::abs6502()
{
		savepc = memory5200[PC] + (memory5200[PC + 1] << 8);
		PC++; PC++;
}

/* Branch */
void C6502::relative6502()
{
		savepc = memory5200[PC++];
		if (savepc & 0x80) savepc -= 0x100;
		if ((savepc>>8) != (PC>>8))
				clockTicks6502++;
}

/* (ABS) */
void C6502::indirect6502()
{
		help = memory5200[PC] + (memory5200[PC + 1] << 8);
		savepc = memory5200[help] + (memory5200[help + 1] << 8);
		PC++; PC++;
}

/* ABS,X */
void C6502::absx6502()
{
		savepc = memory5200[PC] + (memory5200[PC + 1] << 8);
		PC++; PC++;
		if (ticks[opcode]==4)
				if ((savepc>>8) != ((savepc + X)>>8))
						clockTicks6502++;
		savepc += X;
}

/* ABS,Y */
void C6502::absy6502()
{
		savepc = memory5200[PC] + (memory5200[PC + 1] << 8);
		PC++; PC++;

		if (ticks[opcode]==4)
				if ((savepc>>8) != ((savepc + Y)>>8))
						clockTicks6502++;
		savepc += Y;
}

/* ZP */
void C6502::zp6502()
{
		savepc=memory5200[PC++];
}

/* ZP,X */
void C6502::zpx6502()
{
		savepc=memory5200[PC++] + X;
		savepc &= 0x00ff;
}

/* ZP,Y */
void C6502::zpy6502()
{
		savepc=memory5200[PC++] + Y;
		savepc &= 0x00ff;
}

/* (ZP,X) */
void C6502::indx6502()
{
		value = memory5200[PC++] + X;
		savepc = memory5200[value] + (memory5200[value+1] << 8);
}

/* (ZP),Y */
void C6502::indy6502()
{
		value = memory5200[PC++];
		savepc = memory5200[value] + (memory5200[value+1] << 8);
		if (ticks[opcode]==5)
				if ((savepc>>8) != ((savepc + Y)>>8))
						clockTicks6502++;
		savepc += Y;
}

/* (ABS,X) */
void C6502::indabsx6502()
{
		help = memory5200[PC] + (memory5200[PC + 1] << 8) + X;
		savepc = memory5200[help] + (memory5200[help + 1] << 8);
}

/* (ZP) */
void C6502::indzp6502()
{
		value = memory5200[PC++];
		savepc = memory5200[value] + (memory5200[value + 1] << 8);
}



///////////////////////////////////////////////////////////////////////////////
/// Instructions
///////////////////////////////////////////////////////////////////////////////

void C6502::ADC()
{
		(this->*adrmode[opcode])();
		//value = memory5200[savepc];
		// we have to use get6502memory() 'cos some dweeb game coder
		// might have adc'd with some register, with no forethought
		// for poor emu programmers
		value = get6502memory(savepc);
		saveflags = (P & C_FLAG);

		if (P & D_FLAG) {
				// Decimal (BCD) mode
				// This code from Altirra (JH 2016-06-11)
				uint32 lowResult, highResult;
				uint8 flags;
				lowResult = (A & 0xF) + (value & 0xF) + saveflags;
				if (lowResult >= 10)
						lowResult += 6;
				if (lowResult >= 0x20)
						lowResult -= 0x10;
				highResult = (A & 0xf0) + (value & 0xf0) + lowResult;
				flags = P & ~(C_FLAG | N_FLAG | Z_FLAG | V_FLAG);
				flags += (((highResult ^ A) & ~(value ^ A)) >> 1) & V_FLAG;
				flags += (highResult & 0x80);							// update N flag
				if (highResult >= 0xA0)
						highResult += 0x60;
				if (highResult >= 0x100)
						flags += C_FLAG;
				if (!(uint8)(A + value + saveflags))
						flags += Z_FLAG;
				A = (uint8)highResult;
				P = flags;
		} else {
				// Normal/hex mode
				int sum = ((char) A) + ((char) value) + saveflags;
				if ((sum>0x7f) || (sum<-0x80)) P |= V_FLAG;
				else
						P &= V_FLAG_INVERSE;
				sum= A + value + saveflags;
				if (sum>0xff) P |= C_FLAG;
				else
						P &= C_FLAG_INVERSE;
				A=sum;
				clockTicks6502++;
				if (A) P &= Z_FLAG_INVERSE;
				else
						P |= Z_FLAG;
				if (A & 0x80) P |= N_FLAG;
				else
						P &= N_FLAG_INVERSE;
		}

		/* old code
		      sum= ((char) A) + ((char) value) + saveflags;
		      if ((sum>0x7f) || (sum<-0x80)) P |= V_FLAG; else P &= V_FLAG_INVERSE;
		      sum= A + value + saveflags;
		      if (sum>0xff) P |= C_FLAG; else P &= C_FLAG_INVERSE;
		      A=sum;
		      if (P & D_FLAG)
		      {
		              P &= C_FLAG_INVERSE;
		              if ((A & 0x0f)>0x09)
		                      A += 0x06;
		              if ((A & 0xf0)>0x90)
		              {
		                      A += 0x60;
		                      P |= C_FLAG;
		              }
		      }
		      else
		      {
		              clockTicks6502++;
		      }
		
		      if (A) P &= Z_FLAG_INVERSE; else P |= Z_FLAG;
		      if (A & 0x80) P |= N_FLAG; else P &= N_FLAG_INVERSE;
		*/
}

void C6502::AND()
{
		(this->*adrmode[opcode])();
		//value = memory5200[savepc];
		// we have to use get6502memory() 'cos of AND'ing with regs
		value = get6502memory(savepc);
		A &= value;
		if (A) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;	//0x02;
		if (A & 0x80) P |= N_FLAG;	//0x80;
		else P &= N_FLAG_INVERSE;
}

void C6502::ASL()
{
		(this->*adrmode[opcode])();
		//value = memory5200[savepc];
		value = get6502memory(savepc);
		P= (P & 0xfe) | ((value >>7) & 0x01);
		value = value << 1;
		put6502memory(savepc, value);
		if (value) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;	//0x02;
		if (value & 0x80) P |= N_FLAG;	//0x80;
		else P &= N_FLAG_INVERSE;
}

void C6502::ASLA()
{
		P= (P & 0xfe) | ((A >>7) & 0x01);
		A = A << 1;
		if (A) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;
		if (A & 0x80) P |= N_FLAG;
		else P &= N_FLAG_INVERSE;
}

void C6502::BCC()
{
		if ((P & C_FLAG)==0) {
				(this->*adrmode[opcode])();
				PC += savepc;
				clockTicks6502++;
		} else
				value = memory5200[PC++];
}

void C6502::BCS()
{
		if (P & C_FLAG) {
				(this->*adrmode[opcode])();
				PC += savepc;
				clockTicks6502++;
		} else
				value=memory5200[PC++];
}

void C6502::BEQ()
{
		if (P & Z_FLAG) {
				(this->*adrmode[opcode])();
				PC += savepc;
				clockTicks6502++;
		} else
				value=memory5200[PC++];
}

void C6502::BIT()
{
		(this->*adrmode[opcode])();
		//value=memory5200[savepc];
		// we have to use get6502memory() 'cos of BIT'ing regs
		value = get6502memory(savepc);
		/* non-destrucive logically And between value and the accumulator
		 * and set zero flag */
		if (value & A) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;

		/* set negative and overflow flags from value */
		P = (P & 0x3f) | (value & 0xc0);
}

void C6502::BMI()
{
		if (P & N_FLAG) {
				(this->*adrmode[opcode])();
				PC += savepc;
				clockTicks6502++;
		} else
				value=memory5200[PC++];
}

void C6502::BNE()
{
		if ((P & Z_FLAG)==0) {
				(this->*adrmode[opcode])();
				PC += savepc;
				clockTicks6502++;
		} else
				value=memory5200[PC++];
}

void C6502::BPL()
{
		if ((P & N_FLAG)==0) {
				(this->*adrmode[opcode])();
				PC += savepc;
				clockTicks6502++;
		} else
				value=memory5200[PC++];
}

void C6502::BRK()
{
		PC++;
		put6502memory(0x0100+S--, (uint8)(PC>>8));
		put6502memory(0x0100+S--, (uint8)(PC & 0xff));
		put6502memory(0x0100+S--, P);
		P |= B_FLAG; P |= I_FLAG;			//P |= 0x14;
		PC = GETWORD(0xFFFE);
}

void C6502::BVC()
{
		if ((P & V_FLAG)==0) {
				(this->*adrmode[opcode])();
				PC += savepc;
				clockTicks6502++;
		} else
				value=memory5200[PC++];
}

void C6502::BVS()
{
		if (P & V_FLAG) {
				(this->*adrmode[opcode])();
				PC += savepc;
				clockTicks6502++;
		} else
				value=memory5200[PC++];
}

void C6502::CLC()
{
		P &= C_FLAG_INVERSE;
}

void C6502::CLD()
{
		P &= D_FLAG_INVERSE;
}

void C6502::CLI()
{
		P &= I_FLAG_INVERSE;
}

void C6502::CLV()
{
		P &= V_FLAG_INVERSE;
}

void C6502::CMP()
{
		(this->*adrmode[opcode])();
		//value = memory5200[savepc];
		// we have to use get6502memory() 'cos of CMP'ing with regs
		value = get6502memory(savepc);
		if (A+0x100-value>0xff) P |= C_FLAG;
		else P &= C_FLAG_INVERSE;
		value=A+0x100-value;
		if (value) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;
		if (value & 0x80) P |= N_FLAG;
		else P &= N_FLAG_INVERSE;
}

void C6502::CPX()			//cpx6502()
{
		(this->*adrmode[opcode])();
		//value = memory5200[savepc];
		// we have to use get6502memory() 'cos of CPX'ing regs
		value = get6502memory(savepc);
		if (X+0x100-value>0xff) P |= C_FLAG;
		else P &= C_FLAG_INVERSE;
		value=X+0x100-value;
		if (value) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;
		if (value & 0x80) P |= N_FLAG;
		else P &= N_FLAG_INVERSE;
}

void C6502::CPY()			//cpy6502()
{
		(this->*adrmode[opcode])();
		//value = memory5200[savepc];
		// we have to use get6502memory() 'cos of CPY'ing regs
		value = get6502memory(savepc);
		if (Y+0x100-value>0xff) P |= C_FLAG;
		else
				P &= C_FLAG_INVERSE;
		value=Y+0x100-value;
		if (value) P &= Z_FLAG_INVERSE;
		else
				P |= Z_FLAG;
		if (value & 0x80) P |= N_FLAG;
		else
				P &= N_FLAG_INVERSE;
}

void C6502::DEC()			//dec6502()
{
		(this->*adrmode[opcode])();
		// we could be decrementing a register here, so
		// use get/put6502memory()
		// NB: needs checking!!!
		value = get6502memory(savepc);
		value--;
		put6502memory(savepc, value);
		if (value) P &= Z_FLAG_INVERSE;
		else
				P |= Z_FLAG;
		if (value & 0x80) P |= N_FLAG;
		else
				P &= N_FLAG_INVERSE;
}

void C6502::DEX()			//dex6502()
{
		X--;
		if (X) P &= Z_FLAG_INVERSE;
		else
				P |= Z_FLAG;
		if (X & 0x80) P |= N_FLAG;
		else
				P &= N_FLAG_INVERSE;
}

void C6502::DEY()			//dey6502()
{
		Y--;
		if (Y) P &= Z_FLAG_INVERSE;
		else
				P |= Z_FLAG;
		if (Y & 0x80) P |= N_FLAG;
		else
				P &= N_FLAG_INVERSE;
}

void C6502::EOR()			//EOR()
{
		(this->*adrmode[opcode])();
		//A ^= gameImage[savepc];
		A ^= get6502memory(savepc);
		if (A) P &= Z_FLAG_INVERSE;
		else
				P |= Z_FLAG;
		if (A & 0x80) P |= N_FLAG;
		else
				P &= N_FLAG_INVERSE;
}

void C6502::INC()			//inc6502()
{
		(this->*adrmode[opcode])();
		//memory5200[savepc]++;
		//value = gameImage[savepc];
		value = get6502memory(savepc);
		value++;
		put6502memory(savepc, value);
		if (value) P &= Z_FLAG_INVERSE;
		else
				P |= Z_FLAG;
		if (value & 0x80) P |= N_FLAG;
		else
				P &= N_FLAG_INVERSE;
}

void C6502::INX()			//inx6502()
{
		X++;
		if (X) P &= Z_FLAG_INVERSE;
		else
				P |= Z_FLAG;
		if (X & 0x80) P |= N_FLAG;
		else
				P &= N_FLAG_INVERSE;
}

void C6502::INY()			//iny6502()
{
		Y++;
		if (Y) P &= Z_FLAG_INVERSE;
		else
				P |= Z_FLAG;
		if (Y & 0x80) P |= N_FLAG;
		else
				P &= N_FLAG_INVERSE;
}

void C6502::JMP()			//jmp6502()
{
		(this->*adrmode[opcode])();
		PC=savepc;
}

void C6502::JSR()			//jsr6502()
{
		PC++;
		put6502memory(0x0100+S--, (uint8)(PC >> 8));
		put6502memory(0x0100+S--, (uint8)(PC & 0xff));
		PC--;
		(this->*adrmode[opcode])();
		PC = savepc;
}

void C6502::LDA()			//lda6502()
{
		(this->*adrmode[opcode])();
		//    A=memory5200[savepc];
		A=get6502memory(savepc);
		// set the zero flag
		if (A) P &= Z_FLAG_INVERSE;
		else
				P |= Z_FLAG;
		// set the negative flag
		if (A & 0x80) P |= N_FLAG;
		else
				P &= N_FLAG_INVERSE;
}

void C6502::LDX()			//ldx6502()
{
		(this->*adrmode[opcode])();
		//      X=memory5200[savepc];
		X=get6502memory(savepc);
		if (X) P &= Z_FLAG_INVERSE;
		else
				P |= Z_FLAG;
		if (X & 0x80) P |= N_FLAG;
		else
				P &= N_FLAG_INVERSE;
}

void C6502::LDY()			//ldy6502()
{
		(this->*adrmode[opcode])();
		//      Y=memory5200[savepc];
		Y=get6502memory(savepc);
		if (Y) P &= Z_FLAG_INVERSE;
		else
				P |= Z_FLAG;
		if (Y & 0x80) P |= N_FLAG;
		else
				P &= N_FLAG_INVERSE;
}

void C6502::LSR()			//lsr6502()
{
		(this->*adrmode[opcode])();
		//value=memory5200[savepc];
		value = get6502memory(savepc);

		/* set carry flag if shifting right causes a bit to be lost */
		P = (P & 0xfe) | (value & 0x01);

		value = value >>1;
		put6502memory(savepc, value);

		/* set zero flag if value is zero */
		if (value != 0) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;

		/* set negative flag if bit 8 set??? can this happen on an LSR? */
		if ((value & 0x80) == 0x80)
				P |= N_FLAG;
		else
				P &= N_FLAG_INVERSE;
}

void C6502::LSRA()			//lsra6502()
{
		P= (P & 0xfe) | (A & 0x01);
		A = A >>1;
		if (A) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;
		if (A & 0x80) P |= N_FLAG;
		else P &= N_FLAG_INVERSE;
}

void C6502::NOP()				//nop6502()
{
#ifdef DEBUG
	fprintf(stderr, "Opcode NOP or unknown = %2X !\n", opcode);
#endif
}

void C6502::ORA()				//ora6502()
{
		(this->*adrmode[opcode])();
		//A |= memory5200[savepc];
		A |= get6502memory(savepc);
		if (A) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;
		if (A & 0x80) P |= N_FLAG;
		else P &= N_FLAG_INVERSE;
}

void C6502::PHA()				//pha6502()
{
		memory5200[0x100 + S--] = A;
}

void C6502::PHP()				//php6502()
{
		memory5200[0x100 + S--] = P;
}

void C6502::PLA()				//pla6502()
{
		A=memory5200[++S+0x100];
		if (A) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;
		if (A & 0x80) P |= N_FLAG;
		else P &= N_FLAG_INVERSE;
}

void C6502::PLP()				//plp6502()
{
		P = memory5200[++S+0x100] | 0x20;
}

void C6502::ROL()				//rol6502()
{
		saveflags = (P & 0x01);
		(this->*adrmode[opcode])();
		//value = memory5200[savepc];
		value = get6502memory(savepc);
		P = (P & 0xfe) | ((value >>7) & 0x01);
		value = value << 1;
		value |= saveflags;
		put6502memory(savepc, value);
		if (value) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;
		if (value & 0x80) P |= N_FLAG;
		else P &= N_FLAG_INVERSE;
}

void C6502::ROLA()			//rola6502()
{
		saveflags = (P & 0x01);
		P = (P & 0xfe) | ((A >>7) & 0x01);
		A = A << 1;
		A |= saveflags;
		if (A) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;
		if (A & 0x80) P |= N_FLAG;
		else P &= N_FLAG_INVERSE;
}

void C6502::ROR()				//ror6502()
{
		saveflags = (P & 0x01);
		(this->*adrmode[opcode])();
		//value=memory5200[savepc];
		value = get6502memory(savepc);
		P = (P & 0xfe) | (value & 0x01);
		value = value >>1;
		if (saveflags) value |= 0x80;
		put6502memory(savepc, value);
		if (value) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;
		if (value & 0x80) P |= N_FLAG;
		else P &= N_FLAG_INVERSE;
}

void C6502::RORA()			//rora6502()
{
		saveflags = (P & 0x01);
		P = (P & 0xfe) | (A & 0x01);
		A = A >>1;
		if (saveflags) A |= 0x80;
		if (A) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;
		if (A & 0x80) P |= N_FLAG;
		else P &= N_FLAG_INVERSE;
}

// TODO: check if returning from NMI and unset NMI busy flag
// TODO: check if returning from IRQ and unset IRQ busy flag
// check if irq pending after nmi, and do
void C6502::RTI()
{
		if(m_nmiBusy) m_nmiBusy = FALSE;
		if(m_irqBusy) m_irqBusy = FALSE;
		P = memory5200[++S+0x100] | 0x20;
		PC = memory5200[++S + 0x100];
		PC |= (memory5200[++S + 0x100] << 8);

		// do irq if pending
		if(m_irqPending) {
				m_irqPending = FALSE;
				DoIRQ();		//irq6502();
		}
}

void C6502::RTS()
{
		PC = memory5200[++S+0x100];
		PC |= (memory5200[++S+0x100] << 8);
		PC++;
}

void C6502::SBC()
{
		(this->*adrmode[opcode])();
		//value = memory5200[savepc] ^ 0xff;
		value = get6502memory(savepc) ^ 0xFF;
		saveflags = (P & C_FLAG);
		// JH 2016-06-11 new code from Altirra
		if (P & D_FLAG) {
				// Pole Position needs N set properly here for its passing counter
				// to stop correctly!
				uint32 carry7, result, lowResult, highCarry, highResult;

				// Flags set according to binary op
				carry7 = (A & 0x7f) + (value & 0x7f) + saveflags;
				result = carry7 + (A & 0x80) + (value & 0x80);

				// BCD
				lowResult = (A & 15) + (value & 15) + saveflags;
				highCarry = 0x10;
				if (lowResult < 0x10) {
						lowResult -= 6;
						highCarry = 0;
				}

				highResult = (A & 0xf0) + (value & 0xf0) + (lowResult & 0x0f) +
						highCarry;

				if (highResult < 0x100)
						highResult -= 0x60;

				// Set flags
				uint8 p = P & ~(C_FLAG | N_FLAG | Z_FLAG | V_FLAG);
				p += (result & 0x80);				// set N flag
				if (result >= 0x100) p += C_FLAG;
				if (!(result & 0xff))	p += Z_FLAG;
				p += ((result >> 2) ^ (carry7 >> 1)) & V_FLAG;
				P = p;
				A = (uint8)highResult;
		} else {
				int sum = ((char) A) + ((char) value) + (saveflags << 4);
				if ((sum>0x7f) || (sum<-0x80)) P |= V_FLAG;
				else
						P &= V_FLAG_INVERSE;
				sum = A + value + saveflags;
				if (sum>0xff) P |= C_FLAG;
				else
						P &= C_FLAG_INVERSE;
				A=sum;
				clockTicks6502++;
				if (A) P &= Z_FLAG_INVERSE;
				else
						P |= Z_FLAG;
				if (A & 0x80) P |= N_FLAG;
				else
						P &= N_FLAG_INVERSE;
		}

		/* old code
		      sum= ((char) A) + ((char) value) + (saveflags << 4);
		      if ((sum>0x7f) || (sum<-0x80)) P |= V_FLAG; else P &= V_FLAG_INVERSE;
		      sum= A + value + saveflags;
		      if (sum>0xff) P |= C_FLAG; else P &= C_FLAG_INVERSE;
		      A=sum;
		      if (P & 0x08)
		      {
		              A -= 0x66;  
		              P &= C_FLAG_INVERSE;
		              if ((A & 0x0f)>0x09)
		                      A += 0x06;
		              if ((A & 0xf0)>0x90)
		              {
		                      A += 0x60;
		                      P |= C_FLAG;
		              }
		      }
		      else
		      {
		              clockTicks6502++;
		      }
		      if (A) P &= Z_FLAG_INVERSE; else P |= Z_FLAG;
		      if (A & 0x80) P |= N_FLAG; else P &= N_FLAG_INVERSE;
		*/
}

void C6502::SEC()
{
		P |= C_FLAG;
}

void C6502::SED()
{
		P |= D_FLAG;
}

void C6502::SEI()
{
		P |= I_FLAG;
}

void C6502::STA()
{
		(this->*adrmode[opcode])();
		put6502memory(savepc, A);
}

void C6502::STX()
{
		(this->*adrmode[opcode])();
		put6502memory(savepc, X);
}

void C6502::STY()
{
		(this->*adrmode[opcode])();
		put6502memory(savepc, Y);
}

void C6502::TAX()
{
		X = A;
		if (X) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;
		if (X & 0x80) P |= N_FLAG;
		else P &= N_FLAG_INVERSE;
}

void C6502::TAY()
{
		Y = A;
		if (Y) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;
		if (Y & 0x80) P |= N_FLAG;
		else P &= N_FLAG_INVERSE;
}

void C6502::TSX()
{
		X = S;
		if (X) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;
		if (X & 0x80) P |= N_FLAG;
		else P &= N_FLAG_INVERSE;
}

void C6502::TXA()	
{
		A = X;
		if (A) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;
		if (A & 0x80) P |= N_FLAG;
		else P &= N_FLAG_INVERSE;
}

void C6502::TXS()
{
		S = X;
}

void C6502::TYA()
{
		A = Y;
		if (A) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;
		if (A & 0x80) P |= N_FLAG;
		else P &= N_FLAG_INVERSE;
}

void C6502::BRA()
{
		(this->*adrmode[opcode])();
		PC += savepc;
		clockTicks6502++;
}

void C6502::DEA()
{
		A--;
		if (A) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;
		if (A & 0x80) P |= N_FLAG;
		else P &= N_FLAG_INVERSE;
}

void C6502::INA()
{
		A++;
		if (A) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;
		if (A & 0x80) P |= N_FLAG;
		else P &= N_FLAG_INVERSE;
}

void C6502::PHX()	
{
		put6502memory(0x100 + S--, X);
}

void C6502::PLX()	
{
		X = memory5200[++S + 0x100];			// OPTIMISE!
		if (X) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;
		if (X & 0x80) P |= N_FLAG;
		else P &= N_FLAG_INVERSE;
}

void C6502::PHY()
{
		put6502memory(0x100 + S--, Y);		// OPTIMISE!
}

void C6502::PLY()
{
		Y = memory5200[++S + 0x100];			// OPTIMISE!
		if (Y) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;
		if (Y & 0x80) P |= N_FLAG;
		else P &= N_FLAG_INVERSE;
}

void C6502::STZ()
{
		(this->*adrmode[opcode])();
		put6502memory(savepc, 0);
}

void C6502::TSB()
{
		(this->*adrmode[opcode])();
		//memory5200[savepc] |= A;
		value = get6502memory(savepc);
		value |= A;
		put6502memory(savepc, value);
		if (value) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;
}

void C6502::TRB()
{
		(this->*adrmode[opcode])();
		//memory5200[savepc] = memory5200[savepc] & (A ^ 0xff);
		value = get6502memory(savepc);
		value &= (A ^ 0xFF);
		put6502memory(savepc, value);
		if (value) P &= Z_FLAG_INVERSE;
		else P |= Z_FLAG;
}

// Illegal/Unofficial/Undocumented instructions

// ASO  [unofficial - ASL then ORA with Acc]
void C6502::ASO()				//aso6502()
{
#ifdef DEBUG
	fprintf(stderr, "Illegal opcode ASO!\n");
#endif
    (this->*adrmode[opcode])();
		//data = memory5200[savepc];
		uint8 data = get6502memory(savepc);
		// set carry from highest bit before left shift
		if(data & 0x80) P |= C_FLAG;
		else
				P &= C_FLAG_INVERSE;
		A = data << 1;
		if(A & Z_FLAG) P |= Z_FLAG;
		else
				P &= Z_FLAG_INVERSE;
		if(A & N_FLAG) P |= N_FLAG;
		else
				P &= N_FLAG_INVERSE;
}

/* AXA [unofficial - original decode by R.Sterba and R.Petruzela 15.1.1998 :-)]
   AXA - this is our new imaginative name for instruction with opcode hex BB.
   AXA - Store Mem AND #$FD to Acc and X, then set stackpoint to value (Acc - 4)
   It's cool! :-)
*/
void C6502::AXA()					//axa6502()
{
#ifdef DEBUG
	fprintf(stderr, "Illegal opcode AXA!\n");
#endif
    (this->*adrmode[opcode])();

		//A = memory5200[savepc] & 0xFD;
		A = get6502memory(savepc) & 0xFD;
		X = A;
		if(A & Z_FLAG) P |= Z_FLAG;
		else
				P &= Z_FLAG_INVERSE;
		if(A & N_FLAG) P |= N_FLAG;
		else
				P &= N_FLAG_INVERSE;
		S = ((uint16) A - 4) & 0xFF;
}

/* INS [unofficial - INC Mem then SBC with Acc] */
void C6502::INS()				//ins6502()
{
		uint8 data;
#ifdef DEBUG
	fprintf(stderr, "Illegal opcode INS !\n");
#endif
    (this->*adrmode[opcode])();
		//data = ++memory5200[savepc];
		data = get6502memory(savepc);
		data++;
		put6502memory(savepc, data);

		// set flags
		if(data & Z_FLAG) P |= Z_FLAG;
		else
				P &= Z_FLAG_INVERSE;
		if(data & N_FLAG) P |= N_FLAG;
		else
				P &= N_FLAG_INVERSE;
		// do sbc (copied from above, without getting adrmode)
		// (inherits adrmode from this opcode?)
		value = memory5200[savepc] ^ 0xff;
		saveflags=(P & 0x01);
		int sum = ((char) A) + ((char) value) + (saveflags << 4);
		if ((sum>0x7f) || (sum<-0x80))
				P |= V_FLAG;
		else
				P &= V_FLAG_INVERSE;
		sum = A + value + saveflags;
		if (sum>0xff) P |= C_FLAG;
		else
				P &= C_FLAG_INVERSE;
		A = sum;
		if (P & 0x08) {
				A -= 0x66;
				P &= C_FLAG_INVERSE;
				if ((A & 0x0f)>0x09)
						A += 0x06;
				if ((A & 0xf0)>0x90) {
						A += 0x60;
						P |= C_FLAG;
				}
		} else {
				clockTicks6502++;
		}
		if (A) P &= Z_FLAG_INVERSE;
		else
				P |= Z_FLAG;
		if (A & 0x80) P |= N_FLAG;
		else
				P &= N_FLAG_INVERSE;
}

// LAX - Z = N = X = A = getbyte(addr)
void C6502::LAX()					//lax6502()
{
#ifdef DEBUG
	fprintf(stderr, "Illegal opcode LAX!\n");
#endif
    (this->*adrmode[opcode])();
		//A = memory5200[savepc];
		A = get6502memory(savepc);

		X = A;
		if(A & Z_FLAG) P |= Z_FLAG;
		else
				P &= Z_FLAG_INVERSE;
		if(A & N_FLAG) P |= N_FLAG;
		else
				P &= N_FLAG_INVERSE;
}

//   RLA [unofficial - ROL Mem, then AND with A]
// (check this code - it may be wrong)
void C6502::RLA()				//rla6502()
{
#ifdef DEBUG
	fprintf(stderr, "Illegal opcode RLA!\n");
#endif
    (this->*adrmode[opcode])();

		//value = memory5200[savepc];
		value = get6502memory(savepc);

		if(P & C_FLAG) {
				if(value & 0x80) P |= C_FLAG ;
				else
						P &= C_FLAG_INVERSE;
				value = (value << 1) | 1;
				if(value & Z_FLAG) P |= Z_FLAG;
				else
						P &= Z_FLAG_INVERSE;
				if(value & N_FLAG) P |= N_FLAG;
				else
						P &= N_FLAG_INVERSE;
		} else {
				if(value & 0x80) P |= C_FLAG ;
				else
						P &= C_FLAG_INVERSE;
				value = (value << 1);
				if(value & Z_FLAG) P |= Z_FLAG;
				else
						P &= Z_FLAG_INVERSE;
				if(value & N_FLAG) P |= N_FLAG;
				else
						P &= N_FLAG_INVERSE;
		}

		put6502memory(savepc, value);

		value = A & value;
		if(value & Z_FLAG) P |= Z_FLAG;
		else
				P &= Z_FLAG_INVERSE;
		if(value & N_FLAG) P |= N_FLAG;
		else
				P &= N_FLAG_INVERSE;
}

//   RRA [unofficial - ROR Mem, then ADC to Acc]
// (check this code - it may be wrong)
void C6502::RRA()					//rra6502()
{
#ifdef DEBUG
	fprintf(stderr, "Illegal opcode RRA!\n");
#endif
    (this->*adrmode[opcode])();
		//value = memory5200[savepc];
		value = get6502memory(savepc);

		if(P & C_FLAG) {
				P |= (value & 1);				// set C flag from value
				value = (value >> 1) | 0x80;
				if(value & Z_FLAG) P |= Z_FLAG;
				else
						P &= Z_FLAG_INVERSE;
				if(value & N_FLAG) P |= N_FLAG;
				else
						P &= N_FLAG_INVERSE;
		} else {
				P |= (value & 1);
				value = (value >> 1);
				if(value & Z_FLAG) P |= Z_FLAG;
				else
						P &= Z_FLAG_INVERSE;
				if(value & N_FLAG) P |= N_FLAG;
				else
						P &= N_FLAG_INVERSE;
		}

		put6502memory(savepc, value);

		// do ADC
		saveflags=(P & 0x01);
		int sum = ((char) A) + ((char) value) + saveflags;
		if ((sum>0x7f) || (sum<-0x80))
				P |= V_FLAG;
		else
				P &= V_FLAG_INVERSE;
		sum = A + value + saveflags;
		if (sum>0xff) P |= C_FLAG;
		else
				P &= C_FLAG_INVERSE;
		A = sum;
		if (P & 0x08) {
				P &= C_FLAG_INVERSE;
				if ((A & 0x0f)>0x09)
						A += 0x06;
				if ((A & 0xf0)>0x90) {
						A += 0x60;
						P |= C_FLAG;
				}
		} else {
				clockTicks6502++;
		}
		if (A) P &= Z_FLAG_INVERSE;
		else
				P |= Z_FLAG;
		if (A & 0x80) P |= N_FLAG;
		else
				P &= N_FLAG_INVERSE;
}


// SKB - "skip byte"
void C6502::SKB()					//skb6502()
{
#ifdef DEBUG
	DebugPrint("Illegal opcode SKB = %2X !\n", opcode);
#endif
	PC++;
}

// SKW - "skip word"
void C6502::SKW()					//void skw6502()
{
#ifdef DEBUG
	DebugPrint("Illegal opcode SKW = %2X !\n", opcode);
#endif
	PC += 2;
}

// Set up CPU instructions arrays
void C6502::InitialiseInstructions()
{
		//fprintf(logfile, "Initialising 6502 instructions...\n");
		ticks[0x00]=7; instruction[0x00]=&C6502::BRK; adrmode[0x00]=&C6502::implied6502;
		ticks[0x01]=6; instruction[0x01]=&C6502::ORA;	adrmode[0x01]=&C6502::indx6502;
		ticks[0x02]=2; instruction[0x02]=&C6502::NOP;	adrmode[0x02]=&C6502::implied6502;
		ticks[0x03]=2; instruction[0x03]=&C6502::ASO;	adrmode[0x03]=&C6502::indx6502;
		ticks[0x04]=3; instruction[0x04]=&C6502::TSB;	adrmode[0x04]=&C6502::zp6502;
		ticks[0x05]=3; instruction[0x05]=&C6502::ORA;	adrmode[0x05]=&C6502::zp6502;
		ticks[0x06]=5; instruction[0x06]=&C6502::ASL;	adrmode[0x06]=&C6502::zp6502;
		ticks[0x07]=2; instruction[0x07]=&C6502::ASO;	adrmode[0x07]=&C6502::zp6502;
		ticks[0x08]=3; instruction[0x08]=&C6502::PHP;	adrmode[0x08]=&C6502::implied6502;
		ticks[0x09]=3; instruction[0x09]=&C6502::ORA;	adrmode[0x09]=&C6502::immediate6502;
		ticks[0x0a]=2; instruction[0x0a]=&C6502::ASLA; adrmode[0x0a]=&C6502::implied6502;
		ticks[0x0b]=2; instruction[0x0b]=&C6502::ASO; adrmode[0x0b]=&C6502::immediate6502;
		ticks[0x0c]=4; instruction[0x0c]=&C6502::TSB; adrmode[0x0c]=&C6502::abs6502;
		ticks[0x0d]=4; instruction[0x0d]=&C6502::ORA;	adrmode[0x0d]=&C6502::abs6502;
		ticks[0x0e]=6; instruction[0x0e]=&C6502::ASL;	adrmode[0x0e]=&C6502::abs6502;
		ticks[0x0f]=2; instruction[0x0f]=&C6502::ASO;	adrmode[0x0f]=&C6502::abs6502;
		ticks[0x10]=2; instruction[0x10]=&C6502::BPL;	adrmode[0x10]=&C6502::relative6502;
		ticks[0x11]=5; instruction[0x11]=&C6502::ORA;	adrmode[0x11]=&C6502::indy6502;
		ticks[0x12]=3; instruction[0x12]=&C6502::ORA;	adrmode[0x12]=&C6502::indzp6502;
		ticks[0x13]=2; instruction[0x13]=&C6502::ASO;	adrmode[0x13]=&C6502::indy6502;
		ticks[0x14]=3; instruction[0x14]=&C6502::TRB;	adrmode[0x14]=&C6502::zp6502;
		ticks[0x15]=4; instruction[0x15]=&C6502::ORA;	adrmode[0x15]=&C6502::zpx6502;
		ticks[0x16]=6; instruction[0x16]=&C6502::ASL;	adrmode[0x16]=&C6502::zpx6502;
		ticks[0x17]=2; instruction[0x17]=&C6502::ASO;	adrmode[0x17]=&C6502::zpx6502;
		ticks[0x18]=2; instruction[0x18]=&C6502::CLC;	adrmode[0x18]=&C6502::implied6502;
		ticks[0x19]=4; instruction[0x19]=&C6502::ORA;	adrmode[0x19]=&C6502::absy6502;
		ticks[0x1a]=2; instruction[0x1a]=&C6502::INA;	adrmode[0x1a]=&C6502::implied6502;
		ticks[0x1b]=2; instruction[0x1b]=&C6502::ASO;	adrmode[0x1b]=&C6502::absy6502;
		ticks[0x1c]=4; instruction[0x1c]=&C6502::TRB;	adrmode[0x1c]=&C6502::abs6502;
		ticks[0x1d]=4; instruction[0x1d]=&C6502::ORA;	adrmode[0x1d]=&C6502::absx6502;
		ticks[0x1e]=7; instruction[0x1e]=&C6502::ASL;	adrmode[0x1e]=&C6502::absx6502;
		ticks[0x1f]=2; instruction[0x1f]=&C6502::ASO;	adrmode[0x1f]=&C6502::absx6502;
		ticks[0x20]=6; instruction[0x20]=&C6502::JSR;	adrmode[0x20]=&C6502::abs6502;
		ticks[0x21]=6; instruction[0x21]=&C6502::AND;	adrmode[0x21]=&C6502::indx6502;
		ticks[0x22]=2; instruction[0x22]=&C6502::NOP;	adrmode[0x22]=&C6502::implied6502;
		ticks[0x23]=2; instruction[0x23]=&C6502::RLA;	adrmode[0x23]=&C6502::indx6502;
		ticks[0x24]=3; instruction[0x24]=&C6502::BIT;	adrmode[0x24]=&C6502::zp6502;
		ticks[0x25]=3; instruction[0x25]=&C6502::AND;	adrmode[0x25]=&C6502::zp6502;
		ticks[0x26]=5; instruction[0x26]=&C6502::ROL;	adrmode[0x26]=&C6502::zp6502;
		ticks[0x27]=2; instruction[0x27]=&C6502::RLA;	adrmode[0x27]=&C6502::zp6502;
		ticks[0x28]=4; instruction[0x28]=&C6502::PLP;	adrmode[0x28]=&C6502::implied6502;
		ticks[0x29]=3; instruction[0x29]=&C6502::AND;	adrmode[0x29]=&C6502::immediate6502;
		ticks[0x2a]=2; instruction[0x2a]=&C6502::ROLA; adrmode[0x2a]=&C6502::implied6502;
		ticks[0x2b]=2; instruction[0x2b]=&C6502::RLA;	adrmode[0x2b]=&C6502::immediate6502;
		ticks[0x2c]=4; instruction[0x2c]=&C6502::BIT;	adrmode[0x2c]=&C6502::abs6502;
		ticks[0x2d]=4; instruction[0x2d]=&C6502::AND;	adrmode[0x2d]=&C6502::abs6502;
		ticks[0x2e]=6; instruction[0x2e]=&C6502::ROL;	adrmode[0x2e]=&C6502::abs6502;
		ticks[0x2f]=2; instruction[0x2f]=&C6502::RLA;	adrmode[0x2f]=&C6502::abs6502;
		ticks[0x30]=2; instruction[0x30]=&C6502::BMI;	adrmode[0x30]=&C6502::relative6502;
		ticks[0x31]=5; instruction[0x31]=&C6502::AND;	adrmode[0x31]=&C6502::indy6502;
		ticks[0x32]=3; instruction[0x32]=&C6502::AND;	adrmode[0x32]=&C6502::indzp6502;
		ticks[0x33]=2; instruction[0x33]=&C6502::RLA;	adrmode[0x33]=&C6502::indy6502;
		ticks[0x34]=4; instruction[0x34]=&C6502::BIT;	adrmode[0x34]=&C6502::zpx6502;
		ticks[0x35]=4; instruction[0x35]=&C6502::AND;	adrmode[0x35]=&C6502::zpx6502;
		ticks[0x36]=6; instruction[0x36]=&C6502::ROL;	adrmode[0x36]=&C6502::zpx6502;
		ticks[0x37]=2; instruction[0x37]=&C6502::RLA;	adrmode[0x37]=&C6502::zpx6502;
		ticks[0x38]=2; instruction[0x38]=&C6502::SEC;	adrmode[0x38]=&C6502::implied6502;
		ticks[0x39]=4; instruction[0x39]=&C6502::AND;	adrmode[0x39]=&C6502::absy6502;
		ticks[0x3a]=2; instruction[0x3a]=&C6502::DEA;	adrmode[0x3a]=&C6502::implied6502;
		ticks[0x3b]=2; instruction[0x3b]=&C6502::RLA;	adrmode[0x3b]=&C6502::absy6502;
		ticks[0x3c]=4; instruction[0x3c]=&C6502::BIT;	adrmode[0x3c]=&C6502::absx6502;
		ticks[0x3d]=4; instruction[0x3d]=&C6502::AND;	adrmode[0x3d]=&C6502::absx6502;
		ticks[0x3e]=7; instruction[0x3e]=&C6502::ROL;	adrmode[0x3e]=&C6502::absx6502;
		ticks[0x3f]=2; instruction[0x3f]=&C6502::RLA;	adrmode[0x3f]=&C6502::absx6502;
		ticks[0x40]=6; instruction[0x40]=&C6502::RTI;	adrmode[0x40]=&C6502::implied6502;
		ticks[0x41]=6; instruction[0x41]=&C6502::EOR;	adrmode[0x41]=&C6502::indx6502;
		ticks[0x42]=2; instruction[0x42]=&C6502::NOP;	adrmode[0x42]=&C6502::implied6502;
		ticks[0x43]=2; instruction[0x43]=&C6502::NOP;	adrmode[0x43]=&C6502::implied6502;
		ticks[0x44]=2; instruction[0x44]=&C6502::SKB;	adrmode[0x44]=&C6502::implied6502;
		ticks[0x45]=3; instruction[0x45]=&C6502::EOR;	adrmode[0x45]=&C6502::zp6502;
		ticks[0x46]=5; instruction[0x46]=&C6502::LSR;	adrmode[0x46]=&C6502::zp6502;
		ticks[0x47]=2; instruction[0x47]=&C6502::NOP;	adrmode[0x47]=&C6502::implied6502;
		ticks[0x48]=3; instruction[0x48]=&C6502::PHA;	adrmode[0x48]=&C6502::implied6502;
		ticks[0x49]=3; instruction[0x49]=&C6502::EOR;	adrmode[0x49]=&C6502::immediate6502;
		ticks[0x4a]=2; instruction[0x4a]=&C6502::LSRA; adrmode[0x4a]=&C6502::implied6502;
		ticks[0x4b]=2; instruction[0x4b]=&C6502::NOP;	adrmode[0x4b]=&C6502::implied6502;
		ticks[0x4c]=3; instruction[0x4c]=&C6502::JMP;	adrmode[0x4c]=&C6502::abs6502;
		ticks[0x4d]=4; instruction[0x4d]=&C6502::EOR;	adrmode[0x4d]=&C6502::abs6502;
		ticks[0x4e]=6; instruction[0x4e]=&C6502::LSR;	adrmode[0x4e]=&C6502::abs6502;
		ticks[0x4f]=2; instruction[0x4f]=&C6502::NOP;	adrmode[0x4f]=&C6502::implied6502;
		ticks[0x50]=2; instruction[0x50]=&C6502::BVC;	adrmode[0x50]=&C6502::relative6502;
		ticks[0x51]=5; instruction[0x51]=&C6502::EOR;	adrmode[0x51]=&C6502::indy6502;
		ticks[0x52]=3; instruction[0x52]=&C6502::EOR;	adrmode[0x52]=&C6502::indzp6502;
		ticks[0x53]=2; instruction[0x53]=&C6502::NOP;	adrmode[0x53]=&C6502::implied6502;
		ticks[0x54]=2; instruction[0x54]=&C6502::SKB;	adrmode[0x54]=&C6502::implied6502;
		ticks[0x55]=4; instruction[0x55]=&C6502::EOR;	adrmode[0x55]=&C6502::zpx6502;
		ticks[0x56]=6; instruction[0x56]=&C6502::LSR;	adrmode[0x56]=&C6502::zpx6502;
		ticks[0x57]=2; instruction[0x57]=&C6502::NOP;	adrmode[0x57]=&C6502::implied6502;
		ticks[0x58]=2; instruction[0x58]=&C6502::CLI;	adrmode[0x58]=&C6502::implied6502;
		ticks[0x59]=4; instruction[0x59]=&C6502::EOR;	adrmode[0x59]=&C6502::absy6502;
		ticks[0x5a]=3; instruction[0x5a]=&C6502::PHY;	adrmode[0x5a]=&C6502::implied6502;
		ticks[0x5b]=2; instruction[0x5b]=&C6502::NOP;	adrmode[0x5b]=&C6502::implied6502;
		ticks[0x5c]=2; instruction[0x5c]=&C6502::SKW;	adrmode[0x5c]=&C6502::implied6502;
		ticks[0x5d]=4; instruction[0x5d]=&C6502::EOR;	adrmode[0x5d]=&C6502::absx6502;
		ticks[0x5e]=7; instruction[0x5e]=&C6502::LSR;	adrmode[0x5e]=&C6502::absx6502;
		ticks[0x5f]=2; instruction[0x5f]=&C6502::NOP;	adrmode[0x5f]=&C6502::implied6502;
		ticks[0x60]=6; instruction[0x60]=&C6502::RTS;	adrmode[0x60]=&C6502::implied6502;
		ticks[0x61]=6; instruction[0x61]=&C6502::ADC;	adrmode[0x61]=&C6502::indx6502;
		ticks[0x62]=2; instruction[0x62]=&C6502::NOP;	adrmode[0x62]=&C6502::implied6502;
		ticks[0x63]=2; instruction[0x63]=&C6502::RRA;	adrmode[0x63]=&C6502::indx6502;
		ticks[0x64]=3; instruction[0x64]=&C6502::STZ;	adrmode[0x64]=&C6502::zp6502;
		ticks[0x65]=3; instruction[0x65]=&C6502::ADC;	adrmode[0x65]=&C6502::zp6502;
		ticks[0x66]=5; instruction[0x66]=&C6502::ROR;	adrmode[0x66]=&C6502::zp6502;
		ticks[0x67]=2; instruction[0x67]=&C6502::RRA;	adrmode[0x67]=&C6502::zp6502;
		ticks[0x68]=4; instruction[0x68]=&C6502::PLA;	adrmode[0x68]=&C6502::implied6502;
		ticks[0x69]=3; instruction[0x69]=&C6502::ADC;	adrmode[0x69]=&C6502::immediate6502;
		ticks[0x6a]=2; instruction[0x6a]=&C6502::RORA; adrmode[0x6a]=&C6502::implied6502;
		ticks[0x6b]=2; instruction[0x6b]=&C6502::NOP;	adrmode[0x6b]=&C6502::implied6502;
		ticks[0x6c]=5; instruction[0x6c]=&C6502::JMP;	adrmode[0x6c]=&C6502::indirect6502;
		ticks[0x6d]=4; instruction[0x6d]=&C6502::ADC;	adrmode[0x6d]=&C6502::abs6502;
		ticks[0x6e]=6; instruction[0x6e]=&C6502::ROR;	adrmode[0x6e]=&C6502::abs6502;
		ticks[0x6f]=2; instruction[0x6f]=&C6502::RRA;	adrmode[0x6f]=&C6502::abs6502;
		ticks[0x70]=2; instruction[0x70]=&C6502::BVS;	adrmode[0x70]=&C6502::relative6502;
		ticks[0x71]=5; instruction[0x71]=&C6502::ADC;	adrmode[0x71]=&C6502::indy6502;
		ticks[0x72]=3; instruction[0x72]=&C6502::ADC;	adrmode[0x72]=&C6502::indzp6502;
		ticks[0x73]=2; instruction[0x73]=&C6502::RRA;	adrmode[0x73]=&C6502::indy6502;
		ticks[0x74]=4; instruction[0x74]=&C6502::STZ;	adrmode[0x74]=&C6502::zpx6502;
		ticks[0x75]=4; instruction[0x75]=&C6502::ADC;	adrmode[0x75]=&C6502::zpx6502;
		ticks[0x76]=6; instruction[0x76]=&C6502::ROR;	adrmode[0x76]=&C6502::zpx6502;
		ticks[0x77]=2; instruction[0x77]=&C6502::RRA;	adrmode[0x77]=&C6502::zpx6502;
		ticks[0x78]=2; instruction[0x78]=&C6502::SEI;	adrmode[0x78]=&C6502::implied6502;
		ticks[0x79]=4; instruction[0x79]=&C6502::ADC;	adrmode[0x79]=&C6502::absy6502;
		ticks[0x7a]=4; instruction[0x7a]=&C6502::PLY;	adrmode[0x7a]=&C6502::implied6502;
		ticks[0x7b]=2; instruction[0x7b]=&C6502::RRA;	adrmode[0x7b]=&C6502::absy6502;
		ticks[0x7c]=6; instruction[0x7c]=&C6502::JMP;	adrmode[0x7c]=&C6502::indabsx6502;
		ticks[0x7d]=4; instruction[0x7d]=&C6502::ADC;	adrmode[0x7d]=&C6502::absx6502;
		ticks[0x7e]=7; instruction[0x7e]=&C6502::ROR;	adrmode[0x7e]=&C6502::absx6502;
		ticks[0x7f]=2; instruction[0x7f]=&C6502::RRA;	adrmode[0x7f]=&C6502::absx6502;
		ticks[0x80]=2; instruction[0x80]=&C6502::BRA; adrmode[0x80]=&C6502::relative6502;
		ticks[0x81]=6; instruction[0x81]=&C6502::STA; adrmode[0x81]=&C6502::indx6502;
		ticks[0x82]=2; instruction[0x82]=&C6502::SKB; adrmode[0x82]=&C6502::implied6502;
		ticks[0x83]=2; instruction[0x83]=&C6502::NOP; adrmode[0x83]=&C6502::implied6502;
		ticks[0x84]=2; instruction[0x84]=&C6502::STY; adrmode[0x84]=&C6502::zp6502;
		ticks[0x85]=2; instruction[0x85]=&C6502::STA; adrmode[0x85]=&C6502::zp6502;
		ticks[0x86]=2; instruction[0x86]=&C6502::STX; adrmode[0x86]=&C6502::zp6502;
		ticks[0x87]=2; instruction[0x87]=&C6502::NOP; adrmode[0x87]=&C6502::implied6502;
		ticks[0x88]=2; instruction[0x88]=&C6502::DEY; adrmode[0x88]=&C6502::implied6502;
		ticks[0x89]=2; instruction[0x89]=&C6502::BIT; adrmode[0x89]=&C6502::immediate6502;
		ticks[0x8a]=2; instruction[0x8a]=&C6502::TXA; adrmode[0x8a]=&C6502::implied6502;
		ticks[0x8b]=2; instruction[0x8b]=&C6502::NOP; adrmode[0x8b]=&C6502::implied6502;
		ticks[0x8c]=4; instruction[0x8c]=&C6502::STY; adrmode[0x8c]=&C6502::abs6502;
		ticks[0x8d]=4; instruction[0x8d]=&C6502::STA; adrmode[0x8d]=&C6502::abs6502;
		ticks[0x8e]=4; instruction[0x8e]=&C6502::STX; adrmode[0x8e]=&C6502::abs6502;
		ticks[0x8f]=2; instruction[0x8f]=&C6502::NOP; adrmode[0x8f]=&C6502::implied6502;
		ticks[0x90]=2; instruction[0x90]=&C6502::BCC; adrmode[0x90]=&C6502::relative6502;
		ticks[0x91]=6; instruction[0x91]=&C6502::STA; adrmode[0x91]=&C6502::indy6502;
		ticks[0x92]=3; instruction[0x92]=&C6502::STA; adrmode[0x92]=&C6502::indzp6502;
		ticks[0x93]=2; instruction[0x93]=&C6502::NOP; adrmode[0x93]=&C6502::implied6502;
		ticks[0x94]=4; instruction[0x94]=&C6502::STY; adrmode[0x94]=&C6502::zpx6502;
		ticks[0x95]=4; instruction[0x95]=&C6502::STA; adrmode[0x95]=&C6502::zpx6502;
		ticks[0x96]=4; instruction[0x96]=&C6502::STX; adrmode[0x96]=&C6502::zpy6502;
		ticks[0x97]=2; instruction[0x97]=&C6502::NOP; adrmode[0x97]=&C6502::implied6502;
		ticks[0x98]=2; instruction[0x98]=&C6502::TYA; adrmode[0x98]=&C6502::implied6502;
		ticks[0x99]=5; instruction[0x99]=&C6502::STA; adrmode[0x99]=&C6502::absy6502;
		ticks[0x9a]=2; instruction[0x9a]=&C6502::TXS; adrmode[0x9a]=&C6502::implied6502;
		ticks[0x9b]=2; instruction[0x9b]=&C6502::NOP; adrmode[0x9b]=&C6502::implied6502;
		ticks[0x9c]=4; instruction[0x9c]=&C6502::STZ; adrmode[0x9c]=&C6502::abs6502;
		ticks[0x9d]=5; instruction[0x9d]=&C6502::STA; adrmode[0x9d]=&C6502::absx6502;
		ticks[0x9e]=5; instruction[0x9e]=&C6502::STZ; adrmode[0x9e]=&C6502::absx6502;
		ticks[0x9f]=2; instruction[0x9f]=&C6502::NOP; adrmode[0x9f]=&C6502::implied6502;
		ticks[0xa0]=3; instruction[0xa0]=&C6502::LDY; adrmode[0xa0]=&C6502::immediate6502;
		ticks[0xa1]=6; instruction[0xa1]=&C6502::LDA; adrmode[0xa1]=&C6502::indx6502;
		ticks[0xa2]=3; instruction[0xa2]=&C6502::LDX; adrmode[0xa2]=&C6502::immediate6502;
		ticks[0xa3]=2; instruction[0xa3]=&C6502::LAX; adrmode[0xa3]=&C6502::indx6502;
		ticks[0xa4]=3; instruction[0xa4]=&C6502::LDY; adrmode[0xa4]=&C6502::zp6502;
		ticks[0xa5]=3; instruction[0xa5]=&C6502::LDA; adrmode[0xa5]=&C6502::zp6502;
		ticks[0xa6]=3; instruction[0xa6]=&C6502::LDX; adrmode[0xa6]=&C6502::zp6502;
		ticks[0xa7]=2; instruction[0xa7]=&C6502::LAX; adrmode[0xa7]=&C6502::zp6502;
		ticks[0xa8]=2; instruction[0xa8]=&C6502::TAY; adrmode[0xa8]=&C6502::implied6502;
		ticks[0xa9]=3; instruction[0xa9]=&C6502::LDA; adrmode[0xa9]=&C6502::immediate6502;
		ticks[0xaa]=2; instruction[0xaa]=&C6502::TAX; adrmode[0xaa]=&C6502::implied6502;
		ticks[0xab]=2; instruction[0xab]=&C6502::NOP; adrmode[0xab]=&C6502::implied6502;
		ticks[0xac]=4; instruction[0xac]=&C6502::LDY; adrmode[0xac]=&C6502::abs6502;
		ticks[0xad]=4; instruction[0xad]=&C6502::LDA; adrmode[0xad]=&C6502::abs6502;
		ticks[0xae]=4; instruction[0xae]=&C6502::LDX; adrmode[0xae]=&C6502::abs6502;
		ticks[0xaf]=2; instruction[0xaf]=&C6502::LAX; adrmode[0xaf]=&C6502::abs6502;
		ticks[0xb0]=2; instruction[0xb0]=&C6502::BCS; adrmode[0xb0]=&C6502::relative6502;
		ticks[0xb1]=5; instruction[0xb1]=&C6502::LDA; adrmode[0xb1]=&C6502::indy6502;
		ticks[0xb2]=3; instruction[0xb2]=&C6502::LDA; adrmode[0xb2]=&C6502::indzp6502;
		ticks[0xb3]=2; instruction[0xb3]=&C6502::LAX; adrmode[0xb3]=&C6502::indy6502;
		ticks[0xb4]=4; instruction[0xb4]=&C6502::LDY; adrmode[0xb4]=&C6502::zpx6502;
		ticks[0xb5]=4; instruction[0xb5]=&C6502::LDA; adrmode[0xb5]=&C6502::zpx6502;
		ticks[0xb6]=4; instruction[0xb6]=&C6502::LDX; adrmode[0xb6]=&C6502::zpy6502;
		ticks[0xb7]=2; instruction[0xb7]=&C6502::LAX; adrmode[0xb7]=&C6502::zpy6502;
		ticks[0xb8]=2; instruction[0xb8]=&C6502::CLV; adrmode[0xb8]=&C6502::implied6502;
		ticks[0xb9]=4; instruction[0xb9]=&C6502::LDA; adrmode[0xb9]=&C6502::absy6502;
		ticks[0xba]=2; instruction[0xba]=&C6502::TSX; adrmode[0xba]=&C6502::implied6502;
		ticks[0xbb]=2; instruction[0xbb]=&C6502::AXA; adrmode[0xbb]=&C6502::absy6502;
		ticks[0xbc]=4; instruction[0xbc]=&C6502::LDY; adrmode[0xbc]=&C6502::absx6502;
		ticks[0xbd]=4; instruction[0xbd]=&C6502::LDA; adrmode[0xbd]=&C6502::absx6502;
		ticks[0xbe]=4; instruction[0xbe]=&C6502::LDX; adrmode[0xbe]=&C6502::absy6502;
		ticks[0xbf]=2; instruction[0xbf]=&C6502::LAX; adrmode[0xbf]=&C6502::absy6502;
		ticks[0xc0]=3; instruction[0xc0]=&C6502::CPY; adrmode[0xc0]=&C6502::immediate6502;
		ticks[0xc1]=6; instruction[0xc1]=&C6502::CMP; adrmode[0xc1]=&C6502::indx6502;
		ticks[0xc2]=2; instruction[0xc2]=&C6502::SKB; adrmode[0xc2]=&C6502::implied6502;
		ticks[0xc3]=2; instruction[0xc3]=&C6502::NOP; adrmode[0xc3]=&C6502::implied6502;
		ticks[0xc4]=3; instruction[0xc4]=&C6502::CPY; adrmode[0xc4]=&C6502::zp6502;
		ticks[0xc5]=3; instruction[0xc5]=&C6502::CMP; adrmode[0xc5]=&C6502::zp6502;
		ticks[0xc6]=5; instruction[0xc6]=&C6502::DEC; adrmode[0xc6]=&C6502::zp6502;
		ticks[0xc7]=2; instruction[0xc7]=&C6502::NOP; adrmode[0xc7]=&C6502::implied6502;
		ticks[0xc8]=2; instruction[0xc8]=&C6502::INY; adrmode[0xc8]=&C6502::implied6502;
		ticks[0xc9]=3; instruction[0xc9]=&C6502::CMP; adrmode[0xc9]=&C6502::immediate6502;
		ticks[0xca]=2; instruction[0xca]=&C6502::DEX; adrmode[0xca]=&C6502::implied6502;
		ticks[0xcb]=2; instruction[0xcb]=&C6502::NOP; adrmode[0xcb]=&C6502::implied6502;
		ticks[0xcc]=4; instruction[0xcc]=&C6502::CPY; adrmode[0xcc]=&C6502::abs6502;
		ticks[0xcd]=4; instruction[0xcd]=&C6502::CMP; adrmode[0xcd]=&C6502::abs6502;
		ticks[0xce]=6; instruction[0xce]=&C6502::DEC; adrmode[0xce]=&C6502::abs6502;
		ticks[0xcf]=2; instruction[0xcf]=&C6502::NOP; adrmode[0xcf]=&C6502::implied6502;
		ticks[0xd0]=2; instruction[0xd0]=&C6502::BNE; adrmode[0xd0]=&C6502::relative6502;
		ticks[0xd1]=5; instruction[0xd1]=&C6502::CMP; adrmode[0xd1]=&C6502::indy6502;
		ticks[0xd2]=3; instruction[0xd2]=&C6502::CMP; adrmode[0xd2]=&C6502::indzp6502;
		ticks[0xd3]=2; instruction[0xd3]=&C6502::NOP; adrmode[0xd3]=&C6502::implied6502;
		ticks[0xd4]=2; instruction[0xd4]=&C6502::SKB; adrmode[0xd4]=&C6502::implied6502;
		ticks[0xd5]=4; instruction[0xd5]=&C6502::CMP; adrmode[0xd5]=&C6502::zpx6502;
		ticks[0xd6]=6; instruction[0xd6]=&C6502::DEC; adrmode[0xd6]=&C6502::zpx6502;
		ticks[0xd7]=2; instruction[0xd7]=&C6502::NOP; adrmode[0xd7]=&C6502::implied6502;
		ticks[0xd8]=2; instruction[0xd8]=&C6502::CLD; adrmode[0xd8]=&C6502::implied6502;
		ticks[0xd9]=4; instruction[0xd9]=&C6502::CMP; adrmode[0xd9]=&C6502::absy6502;
		ticks[0xda]=3; instruction[0xda]=&C6502::PHX; adrmode[0xda]=&C6502::implied6502;
		ticks[0xdb]=2; instruction[0xdb]=&C6502::NOP; adrmode[0xdb]=&C6502::implied6502;
		ticks[0xdc]=2; instruction[0xdc]=&C6502::SKW; adrmode[0xdc]=&C6502::implied6502;
		ticks[0xdd]=4; instruction[0xdd]=&C6502::CMP; adrmode[0xdd]=&C6502::absx6502;
		ticks[0xde]=7; instruction[0xde]=&C6502::DEC; adrmode[0xde]=&C6502::absx6502;
		ticks[0xdf]=2; instruction[0xdf]=&C6502::NOP; adrmode[0xdf]=&C6502::implied6502;
		ticks[0xe0]=3; instruction[0xe0]=&C6502::CPX; adrmode[0xe0]=&C6502::immediate6502;
		ticks[0xe1]=6; instruction[0xe1]=&C6502::SBC; adrmode[0xe1]=&C6502::indx6502;
		ticks[0xe2]=2; instruction[0xe2]=&C6502::SKB; adrmode[0xe2]=&C6502::implied6502;
		ticks[0xe3]=2; instruction[0xe3]=&C6502::INS; adrmode[0xe3]=&C6502::indx6502;
		ticks[0xe4]=3; instruction[0xe4]=&C6502::CPX; adrmode[0xe4]=&C6502::zp6502;
		ticks[0xe5]=3; instruction[0xe5]=&C6502::SBC; adrmode[0xe5]=&C6502::zp6502;
		ticks[0xe6]=5; instruction[0xe6]=&C6502::INC; adrmode[0xe6]=&C6502::zp6502;
		ticks[0xe7]=2; instruction[0xe7]=&C6502::INS; adrmode[0xe7]=&C6502::zp6502;
		ticks[0xe8]=2; instruction[0xe8]=&C6502::INX; adrmode[0xe8]=&C6502::implied6502;
		ticks[0xe9]=3; instruction[0xe9]=&C6502::SBC; adrmode[0xe9]=&C6502::immediate6502;
		ticks[0xea]=2; instruction[0xea]=&C6502::NOP; adrmode[0xea]=&C6502::implied6502;
		ticks[0xeb]=2; instruction[0xeb]=&C6502::NOP; adrmode[0xeb]=&C6502::implied6502;
		ticks[0xec]=4; instruction[0xec]=&C6502::CPX; adrmode[0xec]=&C6502::abs6502;
		ticks[0xed]=4; instruction[0xed]=&C6502::SBC; adrmode[0xed]=&C6502::abs6502;
		ticks[0xee]=6; instruction[0xee]=&C6502::INC; adrmode[0xee]=&C6502::abs6502;
		ticks[0xef]=2; instruction[0xef]=&C6502::INS; adrmode[0xef]=&C6502::abs6502;
		ticks[0xf0]=2; instruction[0xf0]=&C6502::BEQ; adrmode[0xf0]=&C6502::relative6502;
		ticks[0xf1]=5; instruction[0xf1]=&C6502::SBC; adrmode[0xf1]=&C6502::indy6502;
		ticks[0xf2]=3; instruction[0xf2]=&C6502::SBC; adrmode[0xf2]=&C6502::indzp6502;
		ticks[0xf3]=2; instruction[0xf3]=&C6502::INS; adrmode[0xf3]=&C6502::indy6502;
		ticks[0xf4]=2; instruction[0xf4]=&C6502::SKB; adrmode[0xf4]=&C6502::implied6502;
		ticks[0xf5]=4; instruction[0xf5]=&C6502::SBC; adrmode[0xf5]=&C6502::zpx6502;
		ticks[0xf6]=6; instruction[0xf6]=&C6502::INC; adrmode[0xf6]=&C6502::zpx6502;
		ticks[0xf7]=2; instruction[0xf7]=&C6502::INS; adrmode[0xf7]=&C6502::zpx6502;
		ticks[0xf8]=2; instruction[0xf8]=&C6502::SED; adrmode[0xf8]=&C6502::implied6502;
		ticks[0xf9]=4; instruction[0xf9]=&C6502::SBC; adrmode[0xf9]=&C6502::absy6502;
		ticks[0xfa]=4; instruction[0xfa]=&C6502::PLX; adrmode[0xfa]=&C6502::implied6502;
		ticks[0xfb]=2; instruction[0xfb]=&C6502::INS; adrmode[0xfb]=&C6502::absy6502;
		ticks[0xfc]=2; instruction[0xfc]=&C6502::SKW; adrmode[0xfc]=&C6502::implied6502;
		ticks[0xfd]=4; instruction[0xfd]=&C6502::SBC; adrmode[0xfd]=&C6502::absx6502;
		ticks[0xfe]=7; instruction[0xfe]=&C6502::INC; adrmode[0xfe]=&C6502::absx6502;
		ticks[0xff]=2; instruction[0xff]=&C6502::INS; adrmode[0xff]=&C6502::absx6502;
}

