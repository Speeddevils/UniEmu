/* AMD FidelityFX FSR 1.0 - cálculo das constantes na CPU (licença MIT) */
#include <stdint.h>
#include <math.h>
#define A_CPU 1
#include "fsr/ffx_a.h"
#include "fsr/ffx_fsr1.h"
void ue_fsr_easu_con(unsigned c[16], float vw, float vh, float tw, float th, float ow, float oh) {
    FsrEasuCon(c, c + 4, c + 8, c + 12, vw, vh, tw, th, ow, oh);
}
void ue_fsr_rcas_con(unsigned c[4], float sharpness) { FsrRcasCon(c, sharpness); }
