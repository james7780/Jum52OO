// Struct used by save/load game state

typedef struct {
    unsigned char A, X, Y, F, S, pad1;
    unsigned short PC;
    unsigned char IrqEn, IrqSt;
} STATE_5200;


