// Derived from Neil Bradley's 6502 emulator circa 1998
// Hacked parts are by JH.
// C++ conversion by JH

/* for DEBUG / MONITOR purposes */
#define MONITOR


/* Macros for convenience
#define A a_reg
#define X x_reg
#define Y y_reg
#define P flag_reg
#define S s_reg
#define PC pc_reg

#define N_FLAG 0x80
#define V_FLAG 0x40
#define G_FLAG 0x20
#define B_FLAG 0x10
#define D_FLAG 0x08
#define I_FLAG 0x04
#define Z_FLAG 0x02
#define C_FLAG 0x01
*/

// Flags register bit values
enum FLAGS6502 {
	N_FLAG = 0x80,
	V_FLAG = 0x40,
	G_FLAG = 0x20,
	B_FLAG = 0x10,
	D_FLAG = 0x08,
	I_FLAG = 0x04,
	Z_FLAG = 0x02,
	C_FLAG = 0x01
};

// Flags register inverse bit values (for ANDing)
enum FLAGS6502INVERSE {
	N_FLAG_INVERSE = 0x7F,
	V_FLAG_INVERSE = 0xBF,
	G_FLAG_INVERSE = 0xDF,
	B_FLAG_INVERSE = 0xEF,
	D_FLAG_INVERSE = 0xF7,
	I_FLAG_INVERSE = 0xFB,
	Z_FLAG_INVERSE = 0xFD,
	C_FLAG_INVERSE = 0xFE
};

//extern char space[];

/*
// req for MSVC
void irq6502(void);

// must be called first to initialise all 6502 engines arrays
void init6502(void);

// sets all of the 6502 registers. The program counter is set from
// locations $FFFC and $FFFD masked with the above addrmask
//
void reset6502(void);

// run the 6502 engine for specified number of clock cycles
void exec6502(int n);

void exec6502fast(int n);
void exec6502debug(int n);

void nmi6502();
*/

class C6502
{
public:
	C6502::C6502();					// replaces init6502()
			
	// must be called first to initialise all 6502 engines arrays
	void InitialiseInstructions();		//init6502();
	
	void DoIRQ();					//irq6502();
	void DoNMI();					//nmi6502()

	// Resets all of the 6502 registers. The program counter is set from
	// locations $FFFC and $FFFD
	void Reset();					//reset6502();

	// Run the 6502 engine for specified number of clock cycles
// MOved to C5200 class
//	void Run(int n);			//exec6502(int n);
//	void RunFast(int n);	//exec6502fast(int n);
//	void RunDebug(int n);	//exec6502debug(int n);
	// Execute the next instruction (returns clock ticks taken)
	int ExecuteInstruction();

	uint16 GetPC() { return PC; }
	int GetIRQBusy() { return m_irqBusy; }
	int GetNMIBusy() { return m_nmiBusy; }
	int GetIRQPending() { return m_irqPending; }

private:
	// Set up addressing modes
	// TODO : Should just be one function SetupAddrMode(opcode), with a switch
	void implied6502();				// Handle implied addressing
	void immediate6502();
	void abs6502();
	void relative6502();
	void indirect6502();
	void absx6502();
	void absy6502();
	void zp6502();
	void zpx6502();
	void zpy6502();
	void indx6502();
	void indy6502();
	void indabsx6502();
	void indzp6502();
	
	// Instructions
	void ADC();
	void AND();
	void ASL();
	void ASLA();
	void BCC();
	void BCS();
	void BEQ();
	void BIT();
	void BMI();
	void BNE();
	void BPL();
	void BRK();
	void BVC();
	void BVS();
	void CLC();
	void CLD();
	void CLI();
	void CLV();
	void CMP();
	void CPX();
	void CPY();
	void DEC();
	void DEX();
	void DEY();
	void EOR();
	void INC();
	void INX();
	void INY();
	void JMP();
	void JSR();
	void LDA();
	void LDX();
	void LDY();
	void LSR();
	void LSRA();
	void NOP();
	void ORA();
	void PHA();
	void PHP();
	void PLA();
	void PLP();
	void ROL();
	void ROLA();
	void ROR();
	void RORA();
	void RTI();
	void RTS();
	void SBC();
	void SEC();
	void SED();
	void SEI();
	void STA();
	void STX();
	void STY();
	void TAX();
	void TAY();
	void TSX();
	void TXA();
	void TXS();
	void TYA();
	void BRA();
	void DEA();
	void INA();
	void PHX();
	void PLX();
	void PHY();
	void PLY();
	void STZ();
	void TSB();
	void TRB();
	// Illegal/Unofficial/Undocumented instructions
	void ASO();
	void AXA();
	void INS();
	void LAX();
	void RLA();
	void RRA();
	void SKB();
	void SKW();
	
	
	// Attributes
public:
	//uint8 a_reg, x_reg,y_reg,flag_reg,s_reg;
	//uint16 pc_reg = 0;
	uint8 A, X, Y, P, S;
	uint16 PC;
	
private:
	/* internal registers */
	uint8 opcode;
	int clockTicks6502;
	//int totalTicks;
	//int timerTicks;
	//int ticksToHSYNC;
	//int ticksTillDraw;

	// State variables
	uint16 savepc;
	uint8 value;
	int saveflags;
	uint16 help;
	int m_nmiBusy, m_irqBusy, m_irqPending;
	
	// arrays
	void (C6502::*adrmode[256])();
	void (C6502::*instruction[256])();
	int ticks[256];

};
	