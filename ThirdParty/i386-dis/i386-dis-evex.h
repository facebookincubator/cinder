#ifdef NEED_OPCODE_TABLE

static const struct dis386 evex_table[][256] = {
  /* EVEX_0F */
  {
    /* 00 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 08 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 10 */
    { PREFIX_TABLE (PREFIX_EVEX_0F10) },
    { PREFIX_TABLE (PREFIX_EVEX_0F11) },
    { PREFIX_TABLE (PREFIX_EVEX_0F12) },
    { PREFIX_TABLE (PREFIX_EVEX_0F13) },
    { PREFIX_TABLE (PREFIX_EVEX_0F14) },
    { PREFIX_TABLE (PREFIX_EVEX_0F15) },
    { PREFIX_TABLE (PREFIX_EVEX_0F16) },
    { PREFIX_TABLE (PREFIX_EVEX_0F17) },
    /* 18 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 20 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 28 */
    { PREFIX_TABLE (PREFIX_EVEX_0F28) },
    { PREFIX_TABLE (PREFIX_EVEX_0F29) },
    { PREFIX_TABLE (PREFIX_EVEX_0F2A) },
    { PREFIX_TABLE (PREFIX_EVEX_0F2B) },
    { PREFIX_TABLE (PREFIX_EVEX_0F2C) },
    { PREFIX_TABLE (PREFIX_EVEX_0F2D) },
    { PREFIX_TABLE (PREFIX_EVEX_0F2E) },
    { PREFIX_TABLE (PREFIX_EVEX_0F2F) },
    /* 30 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 38 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 40 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 48 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 50 */
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F51) },
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F54) },
    { PREFIX_TABLE (PREFIX_EVEX_0F55) },
    { PREFIX_TABLE (PREFIX_EVEX_0F56) },
    { PREFIX_TABLE (PREFIX_EVEX_0F57) },
    /* 58 */
    { PREFIX_TABLE (PREFIX_EVEX_0F58) },
    { PREFIX_TABLE (PREFIX_EVEX_0F59) },
    { PREFIX_TABLE (PREFIX_EVEX_0F5A) },
    { PREFIX_TABLE (PREFIX_EVEX_0F5B) },
    { PREFIX_TABLE (PREFIX_EVEX_0F5C) },
    { PREFIX_TABLE (PREFIX_EVEX_0F5D) },
    { PREFIX_TABLE (PREFIX_EVEX_0F5E) },
    { PREFIX_TABLE (PREFIX_EVEX_0F5F) },
    /* 60 */
    { PREFIX_TABLE (PREFIX_EVEX_0F60) },
    { PREFIX_TABLE (PREFIX_EVEX_0F61) },
    { PREFIX_TABLE (PREFIX_EVEX_0F62) },
    { PREFIX_TABLE (PREFIX_EVEX_0F63) },
    { PREFIX_TABLE (PREFIX_EVEX_0F64) },
    { PREFIX_TABLE (PREFIX_EVEX_0F65) },
    { PREFIX_TABLE (PREFIX_EVEX_0F66) },
    { PREFIX_TABLE (PREFIX_EVEX_0F67) },
    /* 68 */
    { PREFIX_TABLE (PREFIX_EVEX_0F68) },
    { PREFIX_TABLE (PREFIX_EVEX_0F69) },
    { PREFIX_TABLE (PREFIX_EVEX_0F6A) },
    { PREFIX_TABLE (PREFIX_EVEX_0F6B) },
    { PREFIX_TABLE (PREFIX_EVEX_0F6C) },
    { PREFIX_TABLE (PREFIX_EVEX_0F6D) },
    { PREFIX_TABLE (PREFIX_EVEX_0F6E) },
    { PREFIX_TABLE (PREFIX_EVEX_0F6F) },
    /* 70 */
    { PREFIX_TABLE (PREFIX_EVEX_0F70) },
    { REG_TABLE (REG_EVEX_0F71) },
    { REG_TABLE (REG_EVEX_0F72) },
    { REG_TABLE (REG_EVEX_0F73) },
    { PREFIX_TABLE (PREFIX_EVEX_0F74) },
    { PREFIX_TABLE (PREFIX_EVEX_0F75) },
    { PREFIX_TABLE (PREFIX_EVEX_0F76) },
    { Bad_Opcode },
    /* 78 */
    { PREFIX_TABLE (PREFIX_EVEX_0F78) },
    { PREFIX_TABLE (PREFIX_EVEX_0F79) },
    { PREFIX_TABLE (PREFIX_EVEX_0F7A) },
    { PREFIX_TABLE (PREFIX_EVEX_0F7B) },
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F7E) },
    { PREFIX_TABLE (PREFIX_EVEX_0F7F) },
    /* 80 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 88 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 90 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 98 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* A0 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* A8 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* B0 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* B8 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* C0 */
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0FC2) },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0FC4) },
    { PREFIX_TABLE (PREFIX_EVEX_0FC5) },
    { PREFIX_TABLE (PREFIX_EVEX_0FC6) },
    { Bad_Opcode },
    /* C8 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* D0 */
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0FD1) },
    { PREFIX_TABLE (PREFIX_EVEX_0FD2) },
    { PREFIX_TABLE (PREFIX_EVEX_0FD3) },
    { PREFIX_TABLE (PREFIX_EVEX_0FD4) },
    { PREFIX_TABLE (PREFIX_EVEX_0FD5) },
    { PREFIX_TABLE (PREFIX_EVEX_0FD6) },
    { Bad_Opcode },
    /* D8 */
    { PREFIX_TABLE (PREFIX_EVEX_0FD8) },
    { PREFIX_TABLE (PREFIX_EVEX_0FD9) },
    { PREFIX_TABLE (PREFIX_EVEX_0FDA) },
    { PREFIX_TABLE (PREFIX_EVEX_0FDB) },
    { PREFIX_TABLE (PREFIX_EVEX_0FDC) },
    { PREFIX_TABLE (PREFIX_EVEX_0FDD) },
    { PREFIX_TABLE (PREFIX_EVEX_0FDE) },
    { PREFIX_TABLE (PREFIX_EVEX_0FDF) },
    /* E0 */
    { PREFIX_TABLE (PREFIX_EVEX_0FE0) },
    { PREFIX_TABLE (PREFIX_EVEX_0FE1) },
    { PREFIX_TABLE (PREFIX_EVEX_0FE2) },
    { PREFIX_TABLE (PREFIX_EVEX_0FE3) },
    { PREFIX_TABLE (PREFIX_EVEX_0FE4) },
    { PREFIX_TABLE (PREFIX_EVEX_0FE5) },
    { PREFIX_TABLE (PREFIX_EVEX_0FE6) },
    { PREFIX_TABLE (PREFIX_EVEX_0FE7) },
    /* E8 */
    { PREFIX_TABLE (PREFIX_EVEX_0FE8) },
    { PREFIX_TABLE (PREFIX_EVEX_0FE9) },
    { PREFIX_TABLE (PREFIX_EVEX_0FEA) },
    { PREFIX_TABLE (PREFIX_EVEX_0FEB) },
    { PREFIX_TABLE (PREFIX_EVEX_0FEC) },
    { PREFIX_TABLE (PREFIX_EVEX_0FED) },
    { PREFIX_TABLE (PREFIX_EVEX_0FEE) },
    { PREFIX_TABLE (PREFIX_EVEX_0FEF) },
    /* F0 */
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0FF1) },
    { PREFIX_TABLE (PREFIX_EVEX_0FF2) },
    { PREFIX_TABLE (PREFIX_EVEX_0FF3) },
    { PREFIX_TABLE (PREFIX_EVEX_0FF4) },
    { PREFIX_TABLE (PREFIX_EVEX_0FF5) },
    { PREFIX_TABLE (PREFIX_EVEX_0FF6) },
    { Bad_Opcode },
    /* F8 */
    { PREFIX_TABLE (PREFIX_EVEX_0FF8) },
    { PREFIX_TABLE (PREFIX_EVEX_0FF9) },
    { PREFIX_TABLE (PREFIX_EVEX_0FFA) },
    { PREFIX_TABLE (PREFIX_EVEX_0FFB) },
    { PREFIX_TABLE (PREFIX_EVEX_0FFC) },
    { PREFIX_TABLE (PREFIX_EVEX_0FFD) },
    { PREFIX_TABLE (PREFIX_EVEX_0FFE) },
    { Bad_Opcode },
  },
  /* EVEX_0F38 */
  {
    /* 00 */
    { PREFIX_TABLE (PREFIX_EVEX_0F3800) },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F3804) },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 08 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F380B) },
    { PREFIX_TABLE (PREFIX_EVEX_0F380C) },
    { PREFIX_TABLE (PREFIX_EVEX_0F380D) },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 10 */
    { PREFIX_TABLE (PREFIX_EVEX_0F3810) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3811) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3812) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3813) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3814) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3815) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3816) },
    { Bad_Opcode },
    /* 18 */
    { PREFIX_TABLE (PREFIX_EVEX_0F3818) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3819) },
    { PREFIX_TABLE (PREFIX_EVEX_0F381A) },
    { PREFIX_TABLE (PREFIX_EVEX_0F381B) },
    { PREFIX_TABLE (PREFIX_EVEX_0F381C) },
    { PREFIX_TABLE (PREFIX_EVEX_0F381D) },
    { PREFIX_TABLE (PREFIX_EVEX_0F381E) },
    { PREFIX_TABLE (PREFIX_EVEX_0F381F) },
    /* 20 */
    { PREFIX_TABLE (PREFIX_EVEX_0F3820) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3821) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3822) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3823) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3824) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3825) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3826) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3827) },
    /* 28 */
    { PREFIX_TABLE (PREFIX_EVEX_0F3828) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3829) },
    { PREFIX_TABLE (PREFIX_EVEX_0F382A) },
    { PREFIX_TABLE (PREFIX_EVEX_0F382B) },
    { PREFIX_TABLE (PREFIX_EVEX_0F382C) },
    { PREFIX_TABLE (PREFIX_EVEX_0F382D) },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 30 */
    { PREFIX_TABLE (PREFIX_EVEX_0F3830) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3831) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3832) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3833) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3834) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3835) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3836) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3837) },
    /* 38 */
    { PREFIX_TABLE (PREFIX_EVEX_0F3838) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3839) },
    { PREFIX_TABLE (PREFIX_EVEX_0F383A) },
    { PREFIX_TABLE (PREFIX_EVEX_0F383B) },
    { PREFIX_TABLE (PREFIX_EVEX_0F383C) },
    { PREFIX_TABLE (PREFIX_EVEX_0F383D) },
    { PREFIX_TABLE (PREFIX_EVEX_0F383E) },
    { PREFIX_TABLE (PREFIX_EVEX_0F383F) },
    /* 40 */
    { PREFIX_TABLE (PREFIX_EVEX_0F3840) },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F3842) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3843) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3844) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3845) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3846) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3847) },
    /* 48 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F384C) },
    { PREFIX_TABLE (PREFIX_EVEX_0F384D) },
    { PREFIX_TABLE (PREFIX_EVEX_0F384E) },
    { PREFIX_TABLE (PREFIX_EVEX_0F384F) },
    /* 50 */
    { PREFIX_TABLE (PREFIX_EVEX_0F3850) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3851) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3852) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3853) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3854) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3855) },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 58 */
    { PREFIX_TABLE (PREFIX_EVEX_0F3858) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3859) },
    { PREFIX_TABLE (PREFIX_EVEX_0F385A) },
    { PREFIX_TABLE (PREFIX_EVEX_0F385B) },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 60 */
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F3862) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3863) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3864) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3865) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3866) },
    { Bad_Opcode },
    /* 68 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 70 */
    { PREFIX_TABLE (PREFIX_EVEX_0F3870) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3871) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3872) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3873) },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F3875) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3876) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3877) },
    /* 78 */
    { PREFIX_TABLE (PREFIX_EVEX_0F3878) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3879) },
    { PREFIX_TABLE (PREFIX_EVEX_0F387A) },
    { PREFIX_TABLE (PREFIX_EVEX_0F387B) },
    { PREFIX_TABLE (PREFIX_EVEX_0F387C) },
    { PREFIX_TABLE (PREFIX_EVEX_0F387D) },
    { PREFIX_TABLE (PREFIX_EVEX_0F387E) },
    { PREFIX_TABLE (PREFIX_EVEX_0F387F) },
    /* 80 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F3883) },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 88 */
    { PREFIX_TABLE (PREFIX_EVEX_0F3888) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3889) },
    { PREFIX_TABLE (PREFIX_EVEX_0F388A) },
    { PREFIX_TABLE (PREFIX_EVEX_0F388B) },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F388D) },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F388F) },
    /* 90 */
    { PREFIX_TABLE (PREFIX_EVEX_0F3890) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3891) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3892) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3893) },
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F3896) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3897) },
    /* 98 */
    { PREFIX_TABLE (PREFIX_EVEX_0F3898) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3899) },
    { PREFIX_TABLE (PREFIX_EVEX_0F389A) },
    { PREFIX_TABLE (PREFIX_EVEX_0F389B) },
    { PREFIX_TABLE (PREFIX_EVEX_0F389C) },
    { PREFIX_TABLE (PREFIX_EVEX_0F389D) },
    { PREFIX_TABLE (PREFIX_EVEX_0F389E) },
    { PREFIX_TABLE (PREFIX_EVEX_0F389F) },
    /* A0 */
    { PREFIX_TABLE (PREFIX_EVEX_0F38A0) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38A1) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38A2) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38A3) },
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F38A6) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38A7) },
    /* A8 */
    { PREFIX_TABLE (PREFIX_EVEX_0F38A8) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38A9) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38AA) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38AB) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38AC) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38AD) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38AE) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38AF) },
    /* B0 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F38B4) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38B5) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38B6) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38B7) },
    /* B8 */
    { PREFIX_TABLE (PREFIX_EVEX_0F38B8) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38B9) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38BA) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38BB) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38BC) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38BD) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38BE) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38BF) },
    /* C0 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F38C4) },
    { Bad_Opcode },
    { REG_TABLE (REG_EVEX_0F38C6) },
    { REG_TABLE (REG_EVEX_0F38C7) },
    /* C8 */
    { PREFIX_TABLE (PREFIX_EVEX_0F38C8) },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F38CA) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38CB) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38CC) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38CD) },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F38CF) },
    /* D0 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* D8 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F38DC) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38DD) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38DE) },
    { PREFIX_TABLE (PREFIX_EVEX_0F38DF) },
    /* E0 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* E8 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* F0 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* F8 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
  },
  /* EVEX_0F3A */
  {
    /* 00 */
    { PREFIX_TABLE (PREFIX_EVEX_0F3A00) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A01) },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A03) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A04) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A05) },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 08 */
    { PREFIX_TABLE (PREFIX_EVEX_0F3A08) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A09) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A0A) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A0B) },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A0F) },
    /* 10 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A14) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A15) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A16) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A17) },
    /* 18 */
    { PREFIX_TABLE (PREFIX_EVEX_0F3A18) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A19) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A1A) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A1B) },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A1D) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A1E) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A1F) },
    /* 20 */
    { PREFIX_TABLE (PREFIX_EVEX_0F3A20) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A21) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A22) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A23) },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A25) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A26) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A27) },
    /* 28 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 30 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 38 */
    { PREFIX_TABLE (PREFIX_EVEX_0F3A38) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A39) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A3A) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A3B) },
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A3E) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A3F) },
    /* 40 */
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A42) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A43) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A44) },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 48 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 50 */
    { PREFIX_TABLE (PREFIX_EVEX_0F3A50) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A51) },
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A54) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A55) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A56) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A57) },
    /* 58 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 60 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A66) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A67) },
    /* 68 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 70 */
    { PREFIX_TABLE (PREFIX_EVEX_0F3A70) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A71) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A72) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3A73) },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 78 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 80 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 88 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 90 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* 98 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* A0 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* A8 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* B0 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* B8 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* C0 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* C8 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F3ACE) },
    { PREFIX_TABLE (PREFIX_EVEX_0F3ACF) },
    /* D0 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* D8 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* E0 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* E8 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* F0 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    /* F8 */
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
    { Bad_Opcode },
  },
};
#endif /* NEED_OPCODE_TABLE */

#ifdef NEED_REG_TABLE
  /* REG_EVEX_0F71 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F71_REG_2) },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F71_REG_4) },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F71_REG_6) },
  },
  /* REG_EVEX_0F72 */
  {
    { PREFIX_TABLE (PREFIX_EVEX_0F72_REG_0) },
    { PREFIX_TABLE (PREFIX_EVEX_0F72_REG_1) },
    { PREFIX_TABLE (PREFIX_EVEX_0F72_REG_2) },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F72_REG_4) },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F72_REG_6) },
  },
  /* REG_EVEX_0F73 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F73_REG_2) },
    { PREFIX_TABLE (PREFIX_EVEX_0F73_REG_3) },
    { Bad_Opcode },
    { Bad_Opcode },
    { PREFIX_TABLE (PREFIX_EVEX_0F73_REG_6) },
    { PREFIX_TABLE (PREFIX_EVEX_0F73_REG_7) },
  },
  /* REG_EVEX_0F38C6 */
  {
    { Bad_Opcode },
    { MOD_TABLE (MOD_EVEX_0F38C6_REG_1) },
    { MOD_TABLE (MOD_EVEX_0F38C6_REG_2) },
    { Bad_Opcode },
    { Bad_Opcode },
    { MOD_TABLE (MOD_EVEX_0F38C6_REG_5) },
    { MOD_TABLE (MOD_EVEX_0F38C6_REG_6) },
  },
  /* REG_EVEX_0F38C7 */
  {
    { Bad_Opcode },
    { MOD_TABLE (MOD_EVEX_0F38C7_REG_1) },
    { MOD_TABLE (MOD_EVEX_0F38C7_REG_2) },
    { Bad_Opcode },
    { Bad_Opcode },
    { MOD_TABLE (MOD_EVEX_0F38C7_REG_5) },
    { MOD_TABLE (MOD_EVEX_0F38C7_REG_6) },
  },
#endif /* NEED_REG_TABLE */

#ifdef NEED_PREFIX_TABLE
  /* PREFIX_EVEX_0F10 */
  {
    { VEX_W_TABLE (EVEX_W_0F10_P_0) },
    { MOD_TABLE (MOD_EVEX_0F10_PREFIX_1) },
    { VEX_W_TABLE (EVEX_W_0F10_P_2) },
    { MOD_TABLE (MOD_EVEX_0F10_PREFIX_3) },
  },
  /* PREFIX_EVEX_0F11 */
  {
    { VEX_W_TABLE (EVEX_W_0F11_P_0) },
    { MOD_TABLE (MOD_EVEX_0F11_PREFIX_1) },
    { VEX_W_TABLE (EVEX_W_0F11_P_2) },
    { MOD_TABLE (MOD_EVEX_0F11_PREFIX_3) },
  },
  /* PREFIX_EVEX_0F12 */
  {
    { MOD_TABLE (MOD_EVEX_0F12_PREFIX_0) },
    { VEX_W_TABLE (EVEX_W_0F12_P_1) },
    { VEX_W_TABLE (EVEX_W_0F12_P_2) },
    { VEX_W_TABLE (EVEX_W_0F12_P_3) },
  },
  /* PREFIX_EVEX_0F13 */
  {
    { VEX_W_TABLE (EVEX_W_0F13_P_0) },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F13_P_2) },
  },
  /* PREFIX_EVEX_0F14 */
  {
    { VEX_W_TABLE (EVEX_W_0F14_P_0) },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F14_P_2) },
  },
  /* PREFIX_EVEX_0F15 */
  {
    { VEX_W_TABLE (EVEX_W_0F15_P_0) },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F15_P_2) },
  },
  /* PREFIX_EVEX_0F16 */
  {
    { MOD_TABLE (MOD_EVEX_0F16_PREFIX_0) },
    { VEX_W_TABLE (EVEX_W_0F16_P_1) },
    { VEX_W_TABLE (EVEX_W_0F16_P_2) },
  },
  /* PREFIX_EVEX_0F17 */
  {
    { VEX_W_TABLE (EVEX_W_0F17_P_0) },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F17_P_2) },
  },
  /* PREFIX_EVEX_0F28 */
  {
    { VEX_W_TABLE (EVEX_W_0F28_P_0) },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F28_P_2) },
  },
  /* PREFIX_EVEX_0F29 */
  {
    { VEX_W_TABLE (EVEX_W_0F29_P_0) },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F29_P_2) },
  },
  /* PREFIX_EVEX_0F2A */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F2A_P_1) },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F2A_P_3) },
  },
  /* PREFIX_EVEX_0F2B */
  {
    { VEX_W_TABLE (EVEX_W_0F2B_P_0) },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F2B_P_2) },
  },
  /* PREFIX_EVEX_0F2C */
  {
    { Bad_Opcode },
    { "vcvttss2si",	{ Gdq, EXxmm_md, EXxEVexS }, 0 },
    { Bad_Opcode },
    { "vcvttsd2si",	{ Gdq, EXxmm_mq, EXxEVexS }, 0 },
  },
  /* PREFIX_EVEX_0F2D */
  {
    { Bad_Opcode },
    { "vcvtss2si",	{ Gdq, EXxmm_md, EXxEVexR }, 0 },
    { Bad_Opcode },
    { "vcvtsd2si",	{ Gdq, EXxmm_mq, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F2E */
  {
    { VEX_W_TABLE (EVEX_W_0F2E_P_0) },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F2E_P_2) },
  },
  /* PREFIX_EVEX_0F2F */
  {
    { VEX_W_TABLE (EVEX_W_0F2F_P_0) },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F2F_P_2) },
  },
  /* PREFIX_EVEX_0F51 */
  {
    { VEX_W_TABLE (EVEX_W_0F51_P_0) },
    { VEX_W_TABLE (EVEX_W_0F51_P_1) },
    { VEX_W_TABLE (EVEX_W_0F51_P_2) },
    { VEX_W_TABLE (EVEX_W_0F51_P_3) },
  },
  /* PREFIX_EVEX_0F54 */
  {
    { VEX_W_TABLE (EVEX_W_0F54_P_0) },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F54_P_2) },
  },
  /* PREFIX_EVEX_0F55 */
  {
    { VEX_W_TABLE (EVEX_W_0F55_P_0) },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F55_P_2) },
  },
  /* PREFIX_EVEX_0F56 */
  {
    { VEX_W_TABLE (EVEX_W_0F56_P_0) },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F56_P_2) },
  },
  /* PREFIX_EVEX_0F57 */
  {
    { VEX_W_TABLE (EVEX_W_0F57_P_0) },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F57_P_2) },
  },
  /* PREFIX_EVEX_0F58 */
  {
    { VEX_W_TABLE (EVEX_W_0F58_P_0) },
    { VEX_W_TABLE (EVEX_W_0F58_P_1) },
    { VEX_W_TABLE (EVEX_W_0F58_P_2) },
    { VEX_W_TABLE (EVEX_W_0F58_P_3) },
  },
  /* PREFIX_EVEX_0F59 */
  {
    { VEX_W_TABLE (EVEX_W_0F59_P_0) },
    { VEX_W_TABLE (EVEX_W_0F59_P_1) },
    { VEX_W_TABLE (EVEX_W_0F59_P_2) },
    { VEX_W_TABLE (EVEX_W_0F59_P_3) },
  },
  /* PREFIX_EVEX_0F5A */
  {
    { VEX_W_TABLE (EVEX_W_0F5A_P_0) },
    { VEX_W_TABLE (EVEX_W_0F5A_P_1) },
    { VEX_W_TABLE (EVEX_W_0F5A_P_2) },
    { VEX_W_TABLE (EVEX_W_0F5A_P_3) },
  },
  /* PREFIX_EVEX_0F5B */
  {
    { VEX_W_TABLE (EVEX_W_0F5B_P_0) },
    { VEX_W_TABLE (EVEX_W_0F5B_P_1) },
    { VEX_W_TABLE (EVEX_W_0F5B_P_2) },
  },
  /* PREFIX_EVEX_0F5C */
  {
    { VEX_W_TABLE (EVEX_W_0F5C_P_0) },
    { VEX_W_TABLE (EVEX_W_0F5C_P_1) },
    { VEX_W_TABLE (EVEX_W_0F5C_P_2) },
    { VEX_W_TABLE (EVEX_W_0F5C_P_3) },
  },
  /* PREFIX_EVEX_0F5D */
  {
    { VEX_W_TABLE (EVEX_W_0F5D_P_0) },
    { VEX_W_TABLE (EVEX_W_0F5D_P_1) },
    { VEX_W_TABLE (EVEX_W_0F5D_P_2) },
    { VEX_W_TABLE (EVEX_W_0F5D_P_3) },
  },
  /* PREFIX_EVEX_0F5E */
  {
    { VEX_W_TABLE (EVEX_W_0F5E_P_0) },
    { VEX_W_TABLE (EVEX_W_0F5E_P_1) },
    { VEX_W_TABLE (EVEX_W_0F5E_P_2) },
    { VEX_W_TABLE (EVEX_W_0F5E_P_3) },
  },
  /* PREFIX_EVEX_0F5F */
  {
    { VEX_W_TABLE (EVEX_W_0F5F_P_0) },
    { VEX_W_TABLE (EVEX_W_0F5F_P_1) },
    { VEX_W_TABLE (EVEX_W_0F5F_P_2) },
    { VEX_W_TABLE (EVEX_W_0F5F_P_3) },
  },
  /* PREFIX_EVEX_0F60 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpunpcklbw",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F61 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpunpcklwd",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F62 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F62_P_2) },
  },
  /* PREFIX_EVEX_0F63 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpacksswb",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F64 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpcmpgtb",	{ XMask, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F65 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpcmpgtw",	{ XMask, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F66 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F66_P_2) },
  },
  /* PREFIX_EVEX_0F67 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpackuswb",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F68 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpunpckhbw",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F69 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpunpckhwd",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F6A */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F6A_P_2) },
  },
  /* PREFIX_EVEX_0F6B */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F6B_P_2) },
  },
  /* PREFIX_EVEX_0F6C */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F6C_P_2) },
  },
  /* PREFIX_EVEX_0F6D */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F6D_P_2) },
  },
  /* PREFIX_EVEX_0F6E */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { EVEX_LEN_TABLE (EVEX_LEN_0F6E_P_2) },
  },
  /* PREFIX_EVEX_0F6F */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F6F_P_1) },
    { VEX_W_TABLE (EVEX_W_0F6F_P_2) },
    { VEX_W_TABLE (EVEX_W_0F6F_P_3) },
  },
  /* PREFIX_EVEX_0F70 */
  {
    { Bad_Opcode },
    { "vpshufhw",	{ XM, EXx, Ib }, 0 },
    { VEX_W_TABLE (EVEX_W_0F70_P_2) },
    { "vpshuflw",	{ XM, EXx, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F71_REG_2 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpsrlw",	{ Vex, EXx, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F71_REG_4 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpsraw",	{ Vex, EXx, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F71_REG_6 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpsllw",	{ Vex, EXx, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F72_REG_0 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpror%LW",	{ Vex, EXx, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F72_REG_1 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vprol%LW",	{ Vex, EXx, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F72_REG_2 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F72_R_2_P_2) },
  },
  /* PREFIX_EVEX_0F72_REG_4 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpsra%LW",	{ Vex, EXx, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F72_REG_6 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F72_R_6_P_2) },
  },
  /* PREFIX_EVEX_0F73_REG_2 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F73_R_2_P_2) },
  },
  /* PREFIX_EVEX_0F73_REG_3 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpsrldq",	{ Vex, EXx, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F73_REG_6 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F73_R_6_P_2) },
  },
  /* PREFIX_EVEX_0F73_REG_7 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpslldq",	{ Vex, EXx, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F74 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpcmpeqb",	{ XMask, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F75 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpcmpeqw",	{ XMask, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F76 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F76_P_2) },
  },
  /* PREFIX_EVEX_0F78 */
  {
    { VEX_W_TABLE (EVEX_W_0F78_P_0) },
    { "vcvttss2usi",	{ Gdq, EXxmm_md, EXxEVexS }, 0 },
    { VEX_W_TABLE (EVEX_W_0F78_P_2) },
    { "vcvttsd2usi",	{ Gdq, EXxmm_mq, EXxEVexS }, 0 },
  },
  /* PREFIX_EVEX_0F79 */
  {
    { VEX_W_TABLE (EVEX_W_0F79_P_0) },
    { "vcvtss2usi",	{ Gdq, EXxmm_md, EXxEVexR }, 0 },
    { VEX_W_TABLE (EVEX_W_0F79_P_2) },
    { "vcvtsd2usi",	{ Gdq, EXxmm_mq, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F7A */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F7A_P_1) },
    { VEX_W_TABLE (EVEX_W_0F7A_P_2) },
    { VEX_W_TABLE (EVEX_W_0F7A_P_3) },
  },
  /* PREFIX_EVEX_0F7B */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F7B_P_1) },
    { VEX_W_TABLE (EVEX_W_0F7B_P_2) },
    { VEX_W_TABLE (EVEX_W_0F7B_P_3) },
  },
  /* PREFIX_EVEX_0F7E */
  {
    { Bad_Opcode },
    { EVEX_LEN_TABLE (EVEX_LEN_0F7E_P_1) },
    { EVEX_LEN_TABLE (EVEX_LEN_0F7E_P_2) },
  },
  /* PREFIX_EVEX_0F7F */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F7F_P_1) },
    { VEX_W_TABLE (EVEX_W_0F7F_P_2) },
    { VEX_W_TABLE (EVEX_W_0F7F_P_3) },
  },
  /* PREFIX_EVEX_0FC2 */
  {
    { VEX_W_TABLE (EVEX_W_0FC2_P_0) },
    { VEX_W_TABLE (EVEX_W_0FC2_P_1) },
    { VEX_W_TABLE (EVEX_W_0FC2_P_2) },
    { VEX_W_TABLE (EVEX_W_0FC2_P_3) },
  },
  /* PREFIX_EVEX_0FC4 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpinsrw",	{ XM, Vex128, Edw, Ib }, 0 },
  },
  /* PREFIX_EVEX_0FC5 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpextrw",	{ Gdq, XS, Ib }, 0 },
  },
  /* PREFIX_EVEX_0FC6 */
  {
    { VEX_W_TABLE (EVEX_W_0FC6_P_0) },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0FC6_P_2) },
  },
  /* PREFIX_EVEX_0FD1 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpsrlw",	{ XM, Vex, EXxmm }, 0 },
  },
  /* PREFIX_EVEX_0FD2 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0FD2_P_2) },
  },
  /* PREFIX_EVEX_0FD3 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0FD3_P_2) },
  },
  /* PREFIX_EVEX_0FD4 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0FD4_P_2) },
  },
  /* PREFIX_EVEX_0FD5 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpmullw",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FD6 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { EVEX_LEN_TABLE (EVEX_LEN_0FD6_P_2) },
  },
  /* PREFIX_EVEX_0FD8 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpsubusb",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FD9 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpsubusw",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FDA */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpminub",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FDB */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpand%LW",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FDC */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpaddusb",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FDD */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpaddusw",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FDE */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpmaxub",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FDF */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpandn%LW",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FE0 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpavgb",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FE1 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpsraw",	{ XM, Vex, EXxmm }, 0 },
  },
  /* PREFIX_EVEX_0FE2 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpsra%LW",	{ XM, Vex, EXxmm }, 0 },
  },
  /* PREFIX_EVEX_0FE3 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpavgw",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FE4 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpmulhuw",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FE5 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpmulhw",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FE6 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0FE6_P_1) },
    { VEX_W_TABLE (EVEX_W_0FE6_P_2) },
    { VEX_W_TABLE (EVEX_W_0FE6_P_3) },
  },
  /* PREFIX_EVEX_0FE7 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0FE7_P_2) },
  },
  /* PREFIX_EVEX_0FE8 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpsubsb",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FE9 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpsubsw",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FEA */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpminsw",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FEB */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpor%LW",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FEC */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpaddsb",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FED */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpaddsw",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FEE */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpmaxsw",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FEF */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpxor%LW",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FF1 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpsllw",	{ XM, Vex, EXxmm }, 0 },
  },
  /* PREFIX_EVEX_0FF2 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0FF2_P_2) },
  },
  /* PREFIX_EVEX_0FF3 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0FF3_P_2) },
  },
  /* PREFIX_EVEX_0FF4 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0FF4_P_2) },
  },
  /* PREFIX_EVEX_0FF5 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpmaddwd",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FF6 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpsadbw",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FF8 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpsubb",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FF9 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpsubw",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FFA */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0FFA_P_2) },
  },
  /* PREFIX_EVEX_0FFB */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0FFB_P_2) },
  },
  /* PREFIX_EVEX_0FFC */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpaddb",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FFD */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpaddw",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0FFE */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0FFE_P_2) },
  },
  /* PREFIX_EVEX_0F3800 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpshufb",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F3804 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpmaddubsw",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F380B */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpmulhrsw",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F380C */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F380C_P_2) },
  },
  /* PREFIX_EVEX_0F380D */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F380D_P_2) },
  },
  /* PREFIX_EVEX_0F3810 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3810_P_1) },
    { VEX_W_TABLE (EVEX_W_0F3810_P_2) },
  },
  /* PREFIX_EVEX_0F3811 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3811_P_1) },
    { VEX_W_TABLE (EVEX_W_0F3811_P_2) },
  },
  /* PREFIX_EVEX_0F3812 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3812_P_1) },
    { VEX_W_TABLE (EVEX_W_0F3812_P_2) },
  },
  /* PREFIX_EVEX_0F3813 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3813_P_1) },
    { VEX_W_TABLE (EVEX_W_0F3813_P_2) },
  },
  /* PREFIX_EVEX_0F3814 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3814_P_1) },
    { "vprorv%LW",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F3815 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3815_P_1) },
    { "vprolv%LW",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F3816 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpermp%XW",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F3818 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3818_P_2) },
  },
  /* PREFIX_EVEX_0F3819 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3819_P_2) },
  },
  /* PREFIX_EVEX_0F381A */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F381A_P_2) },
  },
  /* PREFIX_EVEX_0F381B */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F381B_P_2) },
  },
  /* PREFIX_EVEX_0F381C */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpabsb",	{ XM, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F381D */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpabsw",	{ XM, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F381E */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F381E_P_2) },
  },
  /* PREFIX_EVEX_0F381F */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F381F_P_2) },
  },
  /* PREFIX_EVEX_0F3820 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3820_P_1) },
    { "vpmovsxbw",	{ XM, EXxmmq }, 0 },
  },
  /* PREFIX_EVEX_0F3821 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3821_P_1) },
    { "vpmovsxbd",	{ XM, EXxmmqd }, 0 },
  },
  /* PREFIX_EVEX_0F3822 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3822_P_1) },
    { "vpmovsxbq",	{ XM, EXxmmdw }, 0 },
  },
  /* PREFIX_EVEX_0F3823 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3823_P_1) },
    { "vpmovsxwd",	{ XM, EXxmmq }, 0 },
  },
  /* PREFIX_EVEX_0F3824 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3824_P_1) },
    { "vpmovsxwq",	{ XM, EXxmmqd }, 0 },
  },
  /* PREFIX_EVEX_0F3825 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3825_P_1) },
    { VEX_W_TABLE (EVEX_W_0F3825_P_2) },
  },
  /* PREFIX_EVEX_0F3826 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3826_P_1) },
    { VEX_W_TABLE (EVEX_W_0F3826_P_2) },
  },
  /* PREFIX_EVEX_0F3827 */
  {
    { Bad_Opcode },
    { "vptestnm%LW",	{ XMask, Vex, EXx }, 0 },
    { "vptestm%LW",	{ XMask, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F3828 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3828_P_1) },
    { VEX_W_TABLE (EVEX_W_0F3828_P_2) },
  },
  /* PREFIX_EVEX_0F3829 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3829_P_1) },
    { VEX_W_TABLE (EVEX_W_0F3829_P_2) },
  },
  /* PREFIX_EVEX_0F382A */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F382A_P_1) },
    { VEX_W_TABLE (EVEX_W_0F382A_P_2) },
  },
  /* PREFIX_EVEX_0F382B */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F382B_P_2) },
  },
  /* PREFIX_EVEX_0F382C */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vscalefp%XW",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F382D */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vscalefs%XW",	{ XMScalar, VexScalar, EXxmm_mdq, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F3830 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3830_P_1) },
    { "vpmovzxbw",	{ XM, EXxmmq }, 0 },
  },
  /* PREFIX_EVEX_0F3831 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3831_P_1) },
    { "vpmovzxbd",	{ XM, EXxmmqd }, 0 },
  },
  /* PREFIX_EVEX_0F3832 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3832_P_1) },
    { "vpmovzxbq",	{ XM, EXxmmdw }, 0 },
  },
  /* PREFIX_EVEX_0F3833 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3833_P_1) },
    { "vpmovzxwd",	{ XM, EXxmmq }, 0 },
  },
  /* PREFIX_EVEX_0F3834 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3834_P_1) },
    { "vpmovzxwq",	{ XM, EXxmmqd }, 0 },
  },
  /* PREFIX_EVEX_0F3835 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3835_P_1) },
    { VEX_W_TABLE (EVEX_W_0F3835_P_2) },
  },
  /* PREFIX_EVEX_0F3836 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vperm%LW",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F3837 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3837_P_2) },
  },
  /* PREFIX_EVEX_0F3838 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3838_P_1) },
    { "vpminsb",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F3839 */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3839_P_1) },
    { "vpmins%LW",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F383A */
  {
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F383A_P_1) },
    { "vpminuw",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F383B */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpminu%LW",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F383C */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpmaxsb",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F383D */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpmaxs%LW",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F383E */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpmaxuw",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F383F */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpmaxu%LW",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F3840 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3840_P_2) },
  },
  /* PREFIX_EVEX_0F3842 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vgetexpp%XW",	{ XM, EXx, EXxEVexS }, 0 },
  },
  /* PREFIX_EVEX_0F3843 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vgetexps%XW",	{ XMScalar, VexScalar, EXxmm_mdq, EXxEVexS }, 0 },
  },
  /* PREFIX_EVEX_0F3844 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vplzcnt%LW",	{ XM, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F3845 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpsrlv%LW",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F3846 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpsrav%LW",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F3847 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpsllv%LW",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F384C */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vrcp14p%XW",	{ XM, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F384D */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vrcp14s%XW",	{ XMScalar, VexScalar, EXxmm_mdq }, 0 },
  },
  /* PREFIX_EVEX_0F384E */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vrsqrt14p%XW",	{ XM, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F384F */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vrsqrt14s%XW",	{ XMScalar, VexScalar, EXxmm_mdq }, 0 },
  },
  /* PREFIX_EVEX_0F3850 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpdpbusd",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F3851 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpdpbusds",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F3852 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpdpwssd",	{ XM, Vex, EXx }, 0 },
    { "vp4dpwssd",	{ XM, Vex, EXxmm }, 0 },
  },
  /* PREFIX_EVEX_0F3853 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpdpwssds",	{ XM, Vex, EXx }, 0 },
    { "vp4dpwssds",	{ XM, Vex, EXxmm }, 0 },
  },
  /* PREFIX_EVEX_0F3854 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3854_P_2) },
  },
  /* PREFIX_EVEX_0F3855 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3855_P_2) },
  },
  /* PREFIX_EVEX_0F3858 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3858_P_2) },
  },
  /* PREFIX_EVEX_0F3859 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3859_P_2) },
  },
  /* PREFIX_EVEX_0F385A */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F385A_P_2) },
  },
  /* PREFIX_EVEX_0F385B */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F385B_P_2) },
  },
  /* PREFIX_EVEX_0F3862 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3862_P_2) },
  },
  /* PREFIX_EVEX_0F3863 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3863_P_2) },
  },
  /* PREFIX_EVEX_0F3864 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpblendm%LW",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F3865 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vblendmp%XW",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F3866 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3866_P_2) },
  },
  /* PREFIX_EVEX_0F3870 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3870_P_2) },
  },
  /* PREFIX_EVEX_0F3871 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3871_P_2) },
  },
  /* PREFIX_EVEX_0F3872 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3872_P_2) },
  },
  /* PREFIX_EVEX_0F3873 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3873_P_2) },
  },
  /* PREFIX_EVEX_0F3875 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3875_P_2) },
  },
  /* PREFIX_EVEX_0F3876 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpermi2%LW",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F3877 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpermi2p%XW",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F3878 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3878_P_2) },
  },
  /* PREFIX_EVEX_0F3879 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3879_P_2) },
  },
  /* PREFIX_EVEX_0F387A */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F387A_P_2) },
  },
  /* PREFIX_EVEX_0F387B */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F387B_P_2) },
  },
  /* PREFIX_EVEX_0F387C */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpbroadcastK",	{ XM, Rdq }, 0 },
  },
  /* PREFIX_EVEX_0F387D */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F387D_P_2) },
  },
  /* PREFIX_EVEX_0F387E */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpermt2%LW",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F387F */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpermt2p%XW",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F3883 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3883_P_2) },
  },
  /* PREFIX_EVEX_0F3888 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vexpandp%XW",	{ XM, EXEvexXGscat }, 0 },
  },
  /* PREFIX_EVEX_0F3889 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpexpand%LW",	{ XM, EXEvexXGscat }, 0 },
  },
  /* PREFIX_EVEX_0F388A */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vcompressp%XW",	{ EXEvexXGscat, XM }, 0 },
  },
  /* PREFIX_EVEX_0F388B */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpcompress%LW",	{ EXEvexXGscat, XM }, 0 },
  },
  /* PREFIX_EVEX_0F388D */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F388D_P_2) },
  },
  /* PREFIX_EVEX_0F388F */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpshufbitqmb",  { XMask, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F3890 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpgatherd%LW",	{ XM, MVexVSIBDWpX }, 0 },
  },
  /* PREFIX_EVEX_0F3891 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3891_P_2) },
  },
  /* PREFIX_EVEX_0F3892 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vgatherdp%XW",	{ XM, MVexVSIBDWpX}, 0 },
  },
  /* PREFIX_EVEX_0F3893 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3893_P_2) },
  },
  /* PREFIX_EVEX_0F3896 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfmaddsub132p%XW",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F3897 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfmsubadd132p%XW",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F3898 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfmadd132p%XW",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F3899 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfmadd132s%XW",	{ XMScalar, VexScalar, EXxmm_mdq, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F389A */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfmsub132p%XW",	{ XM, Vex, EXx, EXxEVexR }, 0 },
    { "v4fmaddps",	{ XM, Vex, Mxmm }, 0 },
  },
  /* PREFIX_EVEX_0F389B */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfmsub132s%XW",	{ XMScalar, VexScalar, EXxmm_mdq, EXxEVexR }, 0 },
    { "v4fmaddss",	{ XMScalar, VexScalar, Mxmm }, 0 },
  },
  /* PREFIX_EVEX_0F389C */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfnmadd132p%XW",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F389D */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfnmadd132s%XW",	{ XMScalar, VexScalar, EXxmm_mdq, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F389E */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfnmsub132p%XW",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F389F */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfnmsub132s%XW",	{ XMScalar, VexScalar, EXxmm_mdq, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F38A0 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpscatterd%LW",	{ MVexVSIBDWpX, XM }, 0 },
  },
  /* PREFIX_EVEX_0F38A1 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F38A1_P_2) },
  },
  /* PREFIX_EVEX_0F38A2 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vscatterdp%XW",	{ MVexVSIBDWpX, XM }, 0 },
  },
  /* PREFIX_EVEX_0F38A3 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F38A3_P_2) },
  },
  /* PREFIX_EVEX_0F38A6 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfmaddsub213p%XW",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F38A7 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfmsubadd213p%XW",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F38A8 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfmadd213p%XW",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F38A9 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfmadd213s%XW",	{ XMScalar, VexScalar, EXxmm_mdq, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F38AA */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfmsub213p%XW",	{ XM, Vex, EXx, EXxEVexR }, 0 },
    { "v4fnmaddps",	{ XM, Vex, Mxmm }, 0 },
  },
  /* PREFIX_EVEX_0F38AB */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfmsub213s%XW",	{ XMScalar, VexScalar, EXxmm_mdq, EXxEVexR }, 0 },
    { "v4fnmaddss",	{ XMScalar, VexScalar, Mxmm }, 0 },
  },
  /* PREFIX_EVEX_0F38AC */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfnmadd213p%XW",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F38AD */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfnmadd213s%XW",	{ XMScalar, VexScalar, EXxmm_mdq, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F38AE */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfnmsub213p%XW",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F38AF */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfnmsub213s%XW",	{ XMScalar, VexScalar, EXxmm_mdq, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F38B4 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpmadd52luq",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F38B5 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpmadd52huq",	{ XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F38B6 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfmaddsub231p%XW",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F38B7 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfmsubadd231p%XW",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F38B8 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfmadd231p%XW",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F38B9 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfmadd231s%XW",	{ XMScalar, VexScalar, EXxmm_mdq, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F38BA */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfmsub231p%XW",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F38BB */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfmsub231s%XW",	{ XMScalar, VexScalar, EXxmm_mdq, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F38BC */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfnmadd231p%XW",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F38BD */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfnmadd231s%XW",	{ XMScalar, VexScalar, EXxmm_mdq, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F38BE */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfnmsub231p%XW",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F38BF */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfnmsub231s%XW",	{ XMScalar, VexScalar, EXxmm_mdq, EXxEVexR }, 0 },
  },
  /* PREFIX_EVEX_0F38C4 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpconflict%LW",	{ XM, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F38C6_REG_1 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vgatherpf0dp%XW",  { MVexVSIBDWpX }, 0 },
  },
  /* PREFIX_EVEX_0F38C6_REG_2 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vgatherpf1dp%XW",  { MVexVSIBDWpX }, 0 },
  },
  /* PREFIX_EVEX_0F38C6_REG_5 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vscatterpf0dp%XW",  { MVexVSIBDWpX }, 0 },
  },
  /* PREFIX_EVEX_0F38C6_REG_6 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vscatterpf1dp%XW",  { MVexVSIBDWpX }, 0 },
  },
  /* PREFIX_EVEX_0F38C7_REG_1 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F38C7_R_1_P_2) },
  },
  /* PREFIX_EVEX_0F38C7_REG_2 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F38C7_R_2_P_2) },
  },
  /* PREFIX_EVEX_0F38C7_REG_5 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F38C7_R_5_P_2) },
  },
  /* PREFIX_EVEX_0F38C7_REG_6 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F38C7_R_6_P_2) },
  },
  /* PREFIX_EVEX_0F38C8 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vexp2p%XW",        { XM, EXx, EXxEVexS }, 0 },
  },
  /* PREFIX_EVEX_0F38CA */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vrcp28p%XW",       { XM, EXx, EXxEVexS }, 0 },
  },
  /* PREFIX_EVEX_0F38CB */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vrcp28s%XW",       { XMScalar, VexScalar, EXxmm_mdq, EXxEVexS }, 0 },
  },
  /* PREFIX_EVEX_0F38CC */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vrsqrt28p%XW",     { XM, EXx, EXxEVexS }, 0 },
  },
  /* PREFIX_EVEX_0F38CD */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vrsqrt28s%XW",     { XMScalar, VexScalar, EXxmm_mdq, EXxEVexS }, 0 },
  },
  /* PREFIX_EVEX_0F38CF */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vgf2p8mulb",	{ XM, Vex, EXx }, 0 }, 
  },
  /* PREFIX_EVEX_0F38DC */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vaesenc",       { XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F38DD */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vaesenclast",   { XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F38DE */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vaesdec",       { XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F38DF */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vaesdeclast",   { XM, Vex, EXx }, 0 },
  },
  /* PREFIX_EVEX_0F3A00 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A00_P_2) },
  },
  /* PREFIX_EVEX_0F3A01 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A01_P_2) },
  },
  /* PREFIX_EVEX_0F3A03 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "valign%LW",	{ XM, Vex, EXx, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F3A04 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A04_P_2) },
  },
  /* PREFIX_EVEX_0F3A05 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A05_P_2) },
  },
  /* PREFIX_EVEX_0F3A08 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A08_P_2) },
  },
  /* PREFIX_EVEX_0F3A09 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A09_P_2) },
  },
  /* PREFIX_EVEX_0F3A0A */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A0A_P_2) },
  },
  /* PREFIX_EVEX_0F3A0B */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A0B_P_2) },
  },
  /* PREFIX_EVEX_0F3A0F */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpalignr",	{ XM, Vex, EXx, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F3A14 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpextrb",	{ Edqb, XM, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F3A15 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpextrw",	{ Edqw, XM, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F3A16 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpextrK",	{ Edq, XM, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F3A17 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vextractps",	{ Edqd, XMM, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F3A18 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A18_P_2) },
  },
  /* PREFIX_EVEX_0F3A19 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A19_P_2) },
  },
  /* PREFIX_EVEX_0F3A1A */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A1A_P_2) },
  },
  /* PREFIX_EVEX_0F3A1B */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A1B_P_2) },
  },
  /* PREFIX_EVEX_0F3A1D */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A1D_P_2) },
  },
  /* PREFIX_EVEX_0F3A1E */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpcmpu%LW",	{ XMask, Vex, EXx, VPCMP }, 0 },
  },
  /* PREFIX_EVEX_0F3A1F */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpcmp%LW",	{ XMask, Vex, EXx, VPCMP }, 0 },
  },
  /* PREFIX_EVEX_0F3A20 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpinsrb",	{ XM, Vex128, Edb, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F3A21 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A21_P_2) },
  },
  /* PREFIX_EVEX_0F3A22 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpinsrK",	{ XM, Vex128, Edq, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F3A23 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A23_P_2) },
  },
  /* PREFIX_EVEX_0F3A25 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpternlog%LW",	{ XM, Vex, EXx, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F3A26 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vgetmantp%XW",	{ XM, EXx, EXxEVexS, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F3A27 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vgetmants%XW",	{ XMScalar, VexScalar, EXxmm_mdq, EXxEVexS, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F3A38 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A38_P_2) },
  },
  /* PREFIX_EVEX_0F3A39 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A39_P_2) },
  },
  /* PREFIX_EVEX_0F3A3A */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A3A_P_2) },
  },
  /* PREFIX_EVEX_0F3A3B */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A3B_P_2) },
  },
  /* PREFIX_EVEX_0F3A3E */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A3E_P_2) },
  },
  /* PREFIX_EVEX_0F3A3F */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A3F_P_2) },
  },
  /* PREFIX_EVEX_0F3A42 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A42_P_2) },
  },
  /* PREFIX_EVEX_0F3A43 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A43_P_2) },
  },
  /* PREFIX_EVEX_0F3A44 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vpclmulqdq",	{ XM, Vex, EXx, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F3A50 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A50_P_2) },
  },
  /* PREFIX_EVEX_0F3A51 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A51_P_2) },
  },
  /* PREFIX_EVEX_0F3A54 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfixupimmp%XW",	{ XM, Vex, EXx, EXxEVexS, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F3A55 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { "vfixupimms%XW",	{ XMScalar, VexScalar, EXxmm_mdq, EXxEVexS, Ib }, 0 },
  },
  /* PREFIX_EVEX_0F3A56 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A56_P_2) },
  },
  /* PREFIX_EVEX_0F3A57 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A57_P_2) },
  },
  /* PREFIX_EVEX_0F3A66 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A66_P_2) },
  },
  /* PREFIX_EVEX_0F3A67 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A67_P_2) },
  },
  /* PREFIX_EVEX_0F3A70 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A70_P_2) },
  },
  /* PREFIX_EVEX_0F3A71 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A71_P_2) },
  },
  /* PREFIX_EVEX_0F3A72 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A72_P_2) },
  },
  /* PREFIX_EVEX_0F3A73 */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3A73_P_2) },
  },
  /* PREFIX_EVEX_0F3ACE */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3ACE_P_2) },
  },
  /* PREFIX_EVEX_0F3ACF */
  {
    { Bad_Opcode },
    { Bad_Opcode },
    { VEX_W_TABLE (EVEX_W_0F3ACF_P_2) },
  },
#endif /* NEED_PREFIX_TABLE */

#ifdef NEED_VEX_W_TABLE
  /* EVEX_W_0F10_P_0 */
  {
    { "vmovups",	{ XM, EXEvexXNoBcst }, 0 },
  },
  /* EVEX_W_0F10_P_1_M_0 */
  {
    { "vmovss",	{ XMScalar, EXdScalar }, 0 },
  },
  /* EVEX_W_0F10_P_1_M_1 */
  {
    { "vmovss",	{ XMScalar, VexScalar, EXxmm_md }, 0 },
  },
  /* EVEX_W_0F10_P_2 */
  {
    { Bad_Opcode },
    { "vmovupd",	{ XM, EXEvexXNoBcst }, 0 },
  },
  /* EVEX_W_0F10_P_3_M_0 */
  {
    { Bad_Opcode },
    { "vmovsd",	{ XMScalar, EXqScalar }, 0 },
  },
  /* EVEX_W_0F10_P_3_M_1 */
  {
    { Bad_Opcode },
    { "vmovsd",	{ XMScalar, VexScalar, EXxmm_mq }, 0 },
  },
  /* EVEX_W_0F11_P_0 */
  {
    { "vmovups",	{ EXxS, XM }, 0 },
  },
  /* EVEX_W_0F11_P_1_M_0 */
  {
    { "vmovss",	{ EXdScalarS, XMScalar }, 0 },
  },
  /* EVEX_W_0F11_P_1_M_1 */
  {
    { "vmovss",	{ EXxS, Vex, XMScalar }, 0 },
  },
  /* EVEX_W_0F11_P_2 */
  {
    { Bad_Opcode },
    { "vmovupd",	{ EXxS, XM }, 0 },
  },
  /* EVEX_W_0F11_P_3_M_0 */
  {
    { Bad_Opcode },
    { "vmovsd",	{ EXqScalarS, XMScalar }, 0 },
  },
  /* EVEX_W_0F11_P_3_M_1 */
  {
    { Bad_Opcode },
    { "vmovsd",	{ EXxS, Vex, XMScalar }, 0 },
  },
  /* EVEX_W_0F12_P_0_M_0 */
  {
    { "vmovlps",	{ XMM, Vex, EXxmm_mq }, 0 },
  },
  /* EVEX_W_0F12_P_0_M_1 */
  {
    { "vmovhlps",	{ XMM, Vex, EXxmm_mq }, 0 },
  },
  /* EVEX_W_0F12_P_1 */
  {
    { "vmovsldup",	{ XM, EXEvexXNoBcst }, 0 },
  },
  /* EVEX_W_0F12_P_2 */
  {
    { Bad_Opcode },
    { "vmovlpd",	{ XMM, Vex, EXxmm_mq }, 0 },
  },
  /* EVEX_W_0F12_P_3 */
  {
    { Bad_Opcode },
    { "vmovddup",	{ XM, EXymmq }, 0 },
  },
  /* EVEX_W_0F13_P_0 */
  {
    { "vmovlps",	{ EXxmm_mq, XMM }, 0 },
  },
  /* EVEX_W_0F13_P_2 */
  {
    { Bad_Opcode },
    { "vmovlpd",	{ EXxmm_mq, XMM }, 0 },
  },
  /* EVEX_W_0F14_P_0 */
  {
    { "vunpcklps",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F14_P_2 */
  {
    { Bad_Opcode },
    { "vunpcklpd",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F15_P_0 */
  {
    { "vunpckhps",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F15_P_2 */
  {
    { Bad_Opcode },
    { "vunpckhpd",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F16_P_0_M_0 */
  {
    { "vmovhps",	{ XMM, Vex, EXxmm_mq }, 0 },
  },
  /* EVEX_W_0F16_P_0_M_1 */
  {
    { "vmovlhps",	{ XMM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F16_P_1 */
  {
    { "vmovshdup",	{ XM, EXx }, 0 },
  },
  /* EVEX_W_0F16_P_2 */
  {
    { Bad_Opcode },
    { "vmovhpd",	{ XMM, Vex, EXxmm_mq }, 0 },
  },
  /* EVEX_W_0F17_P_0 */
  {
    { "vmovhps",	{ EXxmm_mq, XMM }, 0 },
  },
  /* EVEX_W_0F17_P_2 */
  {
    { Bad_Opcode },
    { "vmovhpd",	{ EXxmm_mq, XMM }, 0 },
  },
  /* EVEX_W_0F28_P_0 */
  {
    { "vmovaps",	{ XM, EXx }, 0 },
  },
  /* EVEX_W_0F28_P_2 */
  {
    { Bad_Opcode },
    { "vmovapd",	{ XM, EXx }, 0 },
  },
  /* EVEX_W_0F29_P_0 */
  {
    { "vmovaps",	{ EXxS, XM }, 0 },
  },
  /* EVEX_W_0F29_P_2 */
  {
    { Bad_Opcode },
    { "vmovapd",	{ EXxS, XM }, 0 },
  },
  /* EVEX_W_0F2A_P_1 */
  {
    { "vcvtsi2ss%LQ",	{ XMScalar, VexScalar, EXxEVexR, Ed }, 0 },
    { "vcvtsi2ss%LQ",	{ XMScalar, VexScalar, EXxEVexR, Edqa }, 0 },
  },
  /* EVEX_W_0F2A_P_3 */
  {
    { "vcvtsi2sd%LQ",	{ XMScalar, VexScalar, Ed }, 0 },
    { "vcvtsi2sd%LQ",	{ XMScalar, VexScalar, EXxEVexR64, Edqa }, 0 },
  },
  /* EVEX_W_0F2B_P_0 */
  {
    { "vmovntps",	{ EXx, XM }, 0 },
  },
  /* EVEX_W_0F2B_P_2 */
  {
    { Bad_Opcode },
    { "vmovntpd",	{ EXx, XM }, 0 },
  },
  /* EVEX_W_0F2E_P_0 */
  {
    { "vucomiss",	{ XMScalar, EXxmm_md, EXxEVexS }, 0 },
  },
  /* EVEX_W_0F2E_P_2 */
  {
    { Bad_Opcode },
    { "vucomisd",	{ XMScalar, EXxmm_mq, EXxEVexS }, 0 },
  },
  /* EVEX_W_0F2F_P_0 */
  {
    { "vcomiss",	{ XMScalar, EXxmm_md, EXxEVexS }, 0 },
  },
  /* EVEX_W_0F2F_P_2 */
  {
    { Bad_Opcode },
    { "vcomisd",	{ XMScalar, EXxmm_mq, EXxEVexS }, 0 },
  },
  /* EVEX_W_0F51_P_0 */
  {
    { "vsqrtps",	{ XM, EXx, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F51_P_1 */
  {
    { "vsqrtss",	{ XMScalar, VexScalar, EXxmm_md, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F51_P_2 */
  {
    { Bad_Opcode },
    { "vsqrtpd",	{ XM, EXx, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F51_P_3 */
  {
    { Bad_Opcode },
    { "vsqrtsd",	{ XMScalar, VexScalar, EXxmm_mq, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F54_P_0 */
  {
    { "vandps",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F54_P_2 */
  {
    { Bad_Opcode },
    { "vandpd",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F55_P_0 */
  {
    { "vandnps",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F55_P_2 */
  {
    { Bad_Opcode },
    { "vandnpd",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F56_P_0 */
  {
    { "vorps",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F56_P_2 */
  {
    { Bad_Opcode },
    { "vorpd",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F57_P_0 */
  {
    { "vxorps",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F57_P_2 */
  {
    { Bad_Opcode },
    { "vxorpd",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F58_P_0 */
  {
    { "vaddps",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F58_P_1 */
  {
    { "vaddss",	{ XMScalar, VexScalar, EXxmm_md, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F58_P_2 */
  {
    { Bad_Opcode },
    { "vaddpd",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F58_P_3 */
  {
    { Bad_Opcode },
    { "vaddsd",	{ XMScalar, VexScalar, EXxmm_mq, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F59_P_0 */
  {
    { "vmulps",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F59_P_1 */
  {
    { "vmulss",	{ XMScalar, VexScalar, EXxmm_md, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F59_P_2 */
  {
    { Bad_Opcode },
    { "vmulpd",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F59_P_3 */
  {
    { Bad_Opcode },
    { "vmulsd",	{ XMScalar, VexScalar, EXxmm_mq, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F5A_P_0 */
  {
    { "vcvtps2pd",   { XM, EXEvexHalfBcstXmmq, EXxEVexS }, 0 },
  },
  /* EVEX_W_0F5A_P_1 */
  {
    { "vcvtss2sd",	{ XMScalar, VexScalar, EXxmm_md, EXxEVexS }, 0 },
  },
  /* EVEX_W_0F5A_P_2 */
  {
    { Bad_Opcode },
    { "vcvtpd2ps%XY",	{ XMxmmq, EXx, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F5A_P_3 */
  {
    { Bad_Opcode },
    { "vcvtsd2ss",	{ XMScalar, VexScalar, EXxmm_mq, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F5B_P_0 */
  {
    { "vcvtdq2ps",	{ XM, EXx, EXxEVexR }, 0 },
    { "vcvtqq2ps%XY",	{ XMxmmq, EXx, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F5B_P_1 */
  {
    { "vcvttps2dq",	{ XM, EXx, EXxEVexS }, 0 },
  },
  /* EVEX_W_0F5B_P_2 */
  {
    { "vcvtps2dq",	{ XM, EXx, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F5C_P_0 */
  {
    { "vsubps",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F5C_P_1 */
  {
    { "vsubss",	{ XMScalar, VexScalar, EXxmm_md, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F5C_P_2 */
  {
    { Bad_Opcode },
    { "vsubpd",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F5C_P_3 */
  {
    { Bad_Opcode },
    { "vsubsd",	{ XMScalar, VexScalar, EXxmm_mq, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F5D_P_0 */
  {
    { "vminps",	{ XM, Vex, EXx, EXxEVexS }, 0 },
  },
  /* EVEX_W_0F5D_P_1 */
  {
    { "vminss",	{ XMScalar, VexScalar, EXxmm_md, EXxEVexS }, 0 },
  },
  /* EVEX_W_0F5D_P_2 */
  {
    { Bad_Opcode },
    { "vminpd",	{ XM, Vex, EXx, EXxEVexS }, 0 },
  },
  /* EVEX_W_0F5D_P_3 */
  {
    { Bad_Opcode },
    { "vminsd",	{ XMScalar, VexScalar, EXxmm_mq, EXxEVexS }, 0 },
  },
  /* EVEX_W_0F5E_P_0 */
  {
    { "vdivps",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F5E_P_1 */
  {
    { "vdivss",	{ XMScalar, VexScalar, EXxmm_md, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F5E_P_2 */
  {
    { Bad_Opcode },
    { "vdivpd",	{ XM, Vex, EXx, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F5E_P_3 */
  {
    { Bad_Opcode },
    { "vdivsd",	{ XMScalar, VexScalar, EXxmm_mq, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F5F_P_0 */
  {
    { "vmaxps",	{ XM, Vex, EXx, EXxEVexS }, 0 },
  },
  /* EVEX_W_0F5F_P_1 */
  {
    { "vmaxss",	{ XMScalar, VexScalar, EXxmm_md, EXxEVexS }, 0 },
  },
  /* EVEX_W_0F5F_P_2 */
  {
    { Bad_Opcode },
    { "vmaxpd",	{ XM, Vex, EXx, EXxEVexS }, 0 },
  },
  /* EVEX_W_0F5F_P_3 */
  {
    { Bad_Opcode },
    { "vmaxsd",	{ XMScalar, VexScalar, EXxmm_mq, EXxEVexS }, 0 },
  },
  /* EVEX_W_0F62_P_2 */
  {
    { "vpunpckldq",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F66_P_2 */
  {
    { "vpcmpgtd",	{ XMask, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F6A_P_2 */
  {
    { "vpunpckhdq",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F6B_P_2 */
  {
    { "vpackssdw",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F6C_P_2 */
  {
    { Bad_Opcode },
    { "vpunpcklqdq",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F6D_P_2 */
  {
    { Bad_Opcode },
    { "vpunpckhqdq",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F6F_P_1 */
  {
    { "vmovdqu32",	{ XM, EXEvexXNoBcst }, 0 },
    { "vmovdqu64",	{ XM, EXEvexXNoBcst }, 0 },
  },
  /* EVEX_W_0F6F_P_2 */
  {
    { "vmovdqa32",	{ XM, EXEvexXNoBcst }, 0 },
    { "vmovdqa64",	{ XM, EXEvexXNoBcst }, 0 },
  },
  /* EVEX_W_0F6F_P_3 */
  {
    { "vmovdqu8",	{ XM, EXx }, 0 },
    { "vmovdqu16",	{ XM, EXx }, 0 },
  },
  /* EVEX_W_0F70_P_2 */
  {
    { "vpshufd",	{ XM, EXx, Ib }, 0 },
  },
  /* EVEX_W_0F72_R_2_P_2 */
  {
    { "vpsrld",	{ Vex, EXx, Ib }, 0 },
  },
  /* EVEX_W_0F72_R_6_P_2 */
  {
    { "vpslld",	{ Vex, EXx, Ib }, 0 },
  },
  /* EVEX_W_0F73_R_2_P_2 */
  {
    { Bad_Opcode },
    { "vpsrlq",	{ Vex, EXx, Ib }, 0 },
  },
  /* EVEX_W_0F73_R_6_P_2 */
  {
    { Bad_Opcode },
    { "vpsllq",	{ Vex, EXx, Ib }, 0 },
  },
  /* EVEX_W_0F76_P_2 */
  {
    { "vpcmpeqd",	{ XMask, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F78_P_0 */
  {
    { "vcvttps2udq",	{ XM, EXx, EXxEVexS }, 0 },
    { "vcvttpd2udq%XY",	{ XMxmmq, EXx, EXxEVexS }, 0 },
  },
  /* EVEX_W_0F78_P_2 */
  {
    { "vcvttps2uqq",	{ XM, EXEvexHalfBcstXmmq, EXxEVexS }, 0 },
    { "vcvttpd2uqq",	{ XM, EXx, EXxEVexS }, 0 },
  },
  /* EVEX_W_0F79_P_0 */
  {
    { "vcvtps2udq",	{ XM, EXx, EXxEVexR }, 0 },
    { "vcvtpd2udq%XY",	{ XMxmmq, EXx, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F79_P_2 */
  {
    { "vcvtps2uqq",	{ XM, EXEvexHalfBcstXmmq, EXxEVexR }, 0 },
    { "vcvtpd2uqq",	{ XM, EXx, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F7A_P_1 */
  {
    { "vcvtudq2pd",	{ XM, EXEvexHalfBcstXmmq }, 0 },
    { "vcvtuqq2pd",	{ XM, EXx, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F7A_P_2 */
  {
    { "vcvttps2qq",	{ XM, EXEvexHalfBcstXmmq, EXxEVexS }, 0 },
    { "vcvttpd2qq",	{ XM, EXx, EXxEVexS }, 0 },
  },
  /* EVEX_W_0F7A_P_3 */
  {
    { "vcvtudq2ps",	{ XM, EXx, EXxEVexR }, 0 },
    { "vcvtuqq2ps%XY",	{ XMxmmq, EXx, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F7B_P_1 */
  {
    { "vcvtusi2ss%LQ",	{ XMScalar, VexScalar, EXxEVexR, Ed }, 0 },
    { "vcvtusi2ss%LQ",	{ XMScalar, VexScalar, EXxEVexR, Edqa }, 0 },
  },
  /* EVEX_W_0F7B_P_2 */
  {
    { "vcvtps2qq",	{ XM, EXEvexHalfBcstXmmq, EXxEVexR }, 0 },
    { "vcvtpd2qq",	{ XM, EXx, EXxEVexR }, 0 },
  },
  /* EVEX_W_0F7B_P_3 */
  {
    { "vcvtusi2sd%LQ",	{ XMScalar, VexScalar, Ed }, 0 },
    { "vcvtusi2sd%LQ",	{ XMScalar, VexScalar, EXxEVexR64, Edqa }, 0 },
  },
  /* EVEX_W_0F7E_P_1 */
  {
    { Bad_Opcode },
    { "vmovq",	{ XMScalar, EXxmm_mq }, 0 },
  },
  /* EVEX_W_0F7F_P_1 */
  {
    { "vmovdqu32",	{ EXxS, XM }, 0 },
    { "vmovdqu64",	{ EXxS, XM }, 0 },
  },
  /* EVEX_W_0F7F_P_2 */
  {
    { "vmovdqa32",	{ EXxS, XM }, 0 },
    { "vmovdqa64",	{ EXxS, XM }, 0 },
  },
  /* EVEX_W_0F7F_P_3 */
  {
    { "vmovdqu8",	{ EXxS, XM }, 0 },
    { "vmovdqu16",	{ EXxS, XM }, 0 },
  },
  /* EVEX_W_0FC2_P_0 */
  {
    { "vcmpps",	{ XMask, Vex, EXx, EXxEVexS, VCMP }, 0 },
  },
  /* EVEX_W_0FC2_P_1 */
  {
    { "vcmpss",	{ XMask, VexScalar, EXxmm_md, EXxEVexS, VCMP }, 0 },
  },
  /* EVEX_W_0FC2_P_2 */
  {
    { Bad_Opcode },
    { "vcmppd",	{ XMask, Vex, EXx, EXxEVexS, VCMP }, 0 },
  },
  /* EVEX_W_0FC2_P_3 */
  {
    { Bad_Opcode },
    { "vcmpsd",	{ XMask, VexScalar, EXxmm_mq, EXxEVexS, VCMP }, 0 },
  },
  /* EVEX_W_0FC6_P_0 */
  {
    { "vshufps",	{ XM, Vex, EXx, Ib }, 0 },
  },
  /* EVEX_W_0FC6_P_2 */
  {
    { Bad_Opcode },
    { "vshufpd",	{ XM, Vex, EXx, Ib }, 0 },
  },
  /* EVEX_W_0FD2_P_2 */
  {
    { "vpsrld",	{ XM, Vex, EXxmm }, 0 },
  },
  /* EVEX_W_0FD3_P_2 */
  {
    { Bad_Opcode },
    { "vpsrlq",	{ XM, Vex, EXxmm }, 0 },
  },
  /* EVEX_W_0FD4_P_2 */
  {
    { Bad_Opcode },
    { "vpaddq",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0FD6_P_2 */
  {
    { Bad_Opcode },
    { "vmovq",	{ EXxmm_mq, XMScalar }, 0 },
  },
  /* EVEX_W_0FE6_P_1 */
  {
    { "vcvtdq2pd",	{ XM, EXEvexHalfBcstXmmq }, 0 },
    { "vcvtqq2pd",	{ XM, EXx, EXxEVexR }, 0 },
  },
  /* EVEX_W_0FE6_P_2 */
  {
    { Bad_Opcode },
    { "vcvttpd2dq%XY",	{ XMxmmq, EXx, EXxEVexS }, 0 },
  },
  /* EVEX_W_0FE6_P_3 */
  {
    { Bad_Opcode },
    { "vcvtpd2dq%XY",	{ XMxmmq, EXx, EXxEVexR }, 0 },
  },
  /* EVEX_W_0FE7_P_2 */
  {
    { "vmovntdq",	{ EXEvexXNoBcst, XM }, 0 },
  },
  /* EVEX_W_0FF2_P_2 */
  {
    { "vpslld",	{ XM, Vex, EXxmm }, 0 },
  },
  /* EVEX_W_0FF3_P_2 */
  {
    { Bad_Opcode },
    { "vpsllq",	{ XM, Vex, EXxmm }, 0 },
  },
  /* EVEX_W_0FF4_P_2 */
  {
    { Bad_Opcode },
    { "vpmuludq",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0FFA_P_2 */
  {
    { "vpsubd",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0FFB_P_2 */
  {
    { Bad_Opcode },
    { "vpsubq",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0FFE_P_2 */
  {
    { "vpaddd",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F380C_P_2 */
  {
    { "vpermilps",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F380D_P_2 */
  {
    { Bad_Opcode },
    { "vpermilpd",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F3810_P_1 */
  {
    { "vpmovuswb",	{ EXxmmq, XM }, 0 },
  },
  /* EVEX_W_0F3810_P_2 */
  {
    { Bad_Opcode },
    { "vpsrlvw",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F3811_P_1 */
  {
    { "vpmovusdb",	{ EXxmmqd, XM }, 0 },
  },
  /* EVEX_W_0F3811_P_2 */
  {
    { Bad_Opcode },
    { "vpsravw",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F3812_P_1 */
  {
    { "vpmovusqb",	{ EXxmmdw, XM }, 0 },
  },
  /* EVEX_W_0F3812_P_2 */
  {
    { Bad_Opcode },
    { "vpsllvw",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F3813_P_1 */
  {
    { "vpmovusdw",	{ EXxmmq, XM }, 0 },
  },
  /* EVEX_W_0F3813_P_2 */
  {
    { "vcvtph2ps",	{ XM, EXxmmq, EXxEVexS }, 0 },
  },
  /* EVEX_W_0F3814_P_1 */
  {
    { "vpmovusqw",	{ EXxmmqd, XM }, 0 },
  },
  /* EVEX_W_0F3815_P_1 */
  {
    { "vpmovusqd",	{ EXxmmq, XM }, 0 },
  },
  /* EVEX_W_0F3818_P_2 */
  {
    { "vbroadcastss",	{ XM, EXxmm_md }, 0 },
  },
  /* EVEX_W_0F3819_P_2 */
  {
    { "vbroadcastf32x2",	{ XM, EXxmm_mq }, 0 },
    { "vbroadcastsd",	{ XM, EXxmm_mq }, 0 },
  },
  /* EVEX_W_0F381A_P_2 */
  {
    { "vbroadcastf32x4",	{ XM, EXxmm }, 0 },
    { "vbroadcastf64x2",	{ XM, EXxmm }, 0 },
  },
  /* EVEX_W_0F381B_P_2 */
  {
    { "vbroadcastf32x8",	{ XM, EXxmmq }, 0 },
    { "vbroadcastf64x4",	{ XM, EXymm }, 0 },
  },
  /* EVEX_W_0F381E_P_2 */
  {
    { "vpabsd",	{ XM, EXx }, 0 },
  },
  /* EVEX_W_0F381F_P_2 */
  {
    { Bad_Opcode },
    { "vpabsq",	{ XM, EXx }, 0 },
  },
  /* EVEX_W_0F3820_P_1 */
  {
    { "vpmovswb",	{ EXxmmq, XM }, 0 },
  },
  /* EVEX_W_0F3821_P_1 */
  {
    { "vpmovsdb",	{ EXxmmqd, XM }, 0 },
  },
  /* EVEX_W_0F3822_P_1 */
  {
    { "vpmovsqb",	{ EXxmmdw, XM }, 0 },
  },
  /* EVEX_W_0F3823_P_1 */
  {
    { "vpmovsdw",	{ EXxmmq, XM }, 0 },
  },
  /* EVEX_W_0F3824_P_1 */
  {
    { "vpmovsqw",	{ EXxmmqd, XM }, 0 },
  },
  /* EVEX_W_0F3825_P_1 */
  {
    { "vpmovsqd",	{ EXxmmq, XM }, 0 },
  },
  /* EVEX_W_0F3825_P_2 */
  {
    { "vpmovsxdq",	{ XM, EXxmmq }, 0 },
  },
  /* EVEX_W_0F3826_P_1 */
  {
    { "vptestnmb",	{ XMask, Vex, EXx }, 0 },
    { "vptestnmw",	{ XMask, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F3826_P_2 */
  {
    { "vptestmb",	{ XMask, Vex, EXx }, 0 },
    { "vptestmw",	{ XMask, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F3828_P_1 */
  {
    { "vpmovm2b",	{ XM, MaskR }, 0 },
    { "vpmovm2w",	{ XM, MaskR }, 0 },
  },
  /* EVEX_W_0F3828_P_2 */
  {
    { Bad_Opcode },
    { "vpmuldq",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F3829_P_1 */
  {
    { "vpmovb2m",	{ XMask, EXx }, 0 },
    { "vpmovw2m",	{ XMask, EXx }, 0 },
  },
  /* EVEX_W_0F3829_P_2 */
  {
    { Bad_Opcode },
    { "vpcmpeqq",	{ XMask, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F382A_P_1 */
  {
    { Bad_Opcode },
    { "vpbroadcastmb2q",	{ XM, MaskR }, 0 },
  },
  /* EVEX_W_0F382A_P_2 */
  {
    { "vmovntdqa",	{ XM, EXEvexXNoBcst }, 0 },
  },
  /* EVEX_W_0F382B_P_2 */
  {
    { "vpackusdw",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F3830_P_1 */
  {
    { "vpmovwb",	{ EXxmmq, XM }, 0 },
  },
  /* EVEX_W_0F3831_P_1 */
  {
    { "vpmovdb",	{ EXxmmqd, XM }, 0 },
  },
  /* EVEX_W_0F3832_P_1 */
  {
    { "vpmovqb",	{ EXxmmdw, XM }, 0 },
  },
  /* EVEX_W_0F3833_P_1 */
  {
    { "vpmovdw",	{ EXxmmq, XM }, 0 },
  },
  /* EVEX_W_0F3834_P_1 */
  {
    { "vpmovqw",	{ EXxmmqd, XM }, 0 },
  },
  /* EVEX_W_0F3835_P_1 */
  {
    { "vpmovqd",	{ EXxmmq, XM }, 0 },
  },
  /* EVEX_W_0F3835_P_2 */
  {
    { "vpmovzxdq",	{ XM, EXxmmq }, 0 },
  },
  /* EVEX_W_0F3837_P_2 */
  {
    { Bad_Opcode },
    { "vpcmpgtq",	{ XMask, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F3838_P_1 */
  {
    { "vpmovm2d",	{ XM, MaskR }, 0 },
    { "vpmovm2q",	{ XM, MaskR }, 0 },
  },
  /* EVEX_W_0F3839_P_1 */
  {
    { "vpmovd2m",	{ XMask, EXx }, 0 },
    { "vpmovq2m",	{ XMask, EXx }, 0 },
  },
  /* EVEX_W_0F383A_P_1 */
  {
    { "vpbroadcastmw2d",	{ XM, MaskR }, 0 },
  },
  /* EVEX_W_0F3840_P_2 */
  {
    { "vpmulld",	{ XM, Vex, EXx }, 0 },
    { "vpmullq",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F3854_P_2 */
  {
    { "vpopcntb",	{ XM, EXx }, 0 },
    { "vpopcntw",	{ XM, EXx }, 0 },
  },
  /* EVEX_W_0F3855_P_2 */
  {
    { "vpopcntd",	{ XM, EXx }, 0 },
    { "vpopcntq",	{ XM, EXx }, 0 },
  },
  /* EVEX_W_0F3858_P_2 */
  {
    { "vpbroadcastd",	{ XM, EXxmm_md }, 0 },
  },
  /* EVEX_W_0F3859_P_2 */
  {
    { "vbroadcasti32x2",	{ XM, EXxmm_mq }, 0 },
    { "vpbroadcastq",	{ XM, EXxmm_mq }, 0 },
  },
  /* EVEX_W_0F385A_P_2 */
  {
    { "vbroadcasti32x4",	{ XM, EXxmm }, 0 },
    { "vbroadcasti64x2",	{ XM, EXxmm }, 0 },
  },
  /* EVEX_W_0F385B_P_2 */
  {
    { "vbroadcasti32x8",	{ XM, EXxmmq }, 0 },
    { "vbroadcasti64x4",	{ XM, EXymm }, 0 },
  },
  /* EVEX_W_0F3862_P_2 */
  {
    { "vpexpandb", { XM, EXbScalar }, 0 },
    { "vpexpandw", { XM, EXwScalar }, 0 },
  },
  /* EVEX_W_0F3863_P_2 */
  {
    { "vpcompressb",   { EXbScalar, XM }, 0 },
    { "vpcompressw",   { EXwScalar, XM }, 0 },
  },
  /* EVEX_W_0F3866_P_2 */
  {
    { "vpblendmb",	{ XM, Vex, EXx }, 0 },
    { "vpblendmw",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F3870_P_2 */
  {
    { Bad_Opcode },
    { "vpshldvw",  { XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F3871_P_2 */
  {
    { "vpshldvd",  { XM, Vex, EXx }, 0 },
    { "vpshldvq",  { XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F3872_P_2 */
  {
    { Bad_Opcode },
    { "vpshrdvw",  { XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F3873_P_2 */
  {
    { "vpshrdvd",  { XM, Vex, EXx }, 0 },
    { "vpshrdvq",  { XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F3875_P_2 */
  {
    { "vpermi2b",	{ XM, Vex, EXx }, 0 },
    { "vpermi2w",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F3878_P_2 */
  {
    { "vpbroadcastb",	{ XM, EXxmm_mb }, 0 },
  },
  /* EVEX_W_0F3879_P_2 */
  {
    { "vpbroadcastw",	{ XM, EXxmm_mw }, 0 },
  },
  /* EVEX_W_0F387A_P_2 */
  {
    { "vpbroadcastb",	{ XM, Rd }, 0 },
  },
  /* EVEX_W_0F387B_P_2 */
  {
    { "vpbroadcastw",	{ XM, Rd }, 0 },
  },
  /* EVEX_W_0F387D_P_2 */
  {
    { "vpermt2b",	{ XM, Vex, EXx }, 0 },
    { "vpermt2w",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F3883_P_2 */
  {
    { Bad_Opcode },
    { "vpmultishiftqb",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F388D_P_2 */
  {
    { "vpermb",	{ XM, Vex, EXx }, 0 },
    { "vpermw",	{ XM, Vex, EXx }, 0 },
  },
  /* EVEX_W_0F3891_P_2 */
  {
    { "vpgatherqd",	{ XMxmmq, MVexVSIBQDWpX }, 0 },
    { "vpgatherqq",	{ XM, MVexVSIBQWpX }, 0 },
  },
  /* EVEX_W_0F3893_P_2 */
  {
    { "vgatherqps",	{ XMxmmq, MVexVSIBQDWpX }, 0 },
    { "vgatherqpd",	{ XM, MVexVSIBQWpX }, 0 },
  },
  /* EVEX_W_0F38A1_P_2 */
  {
    { "vpscatterqd",	{ MVexVSIBQDWpX, XMxmmq }, 0 },
    { "vpscatterqq",	{ MVexVSIBQWpX, XM }, 0 },
  },
  /* EVEX_W_0F38A3_P_2 */
  {
    { "vscatterqps",	{ MVexVSIBQDWpX, XMxmmq }, 0 },
    { "vscatterqpd",	{ MVexVSIBQWpX, XM }, 0 },
  },
  /* EVEX_W_0F38C7_R_1_P_2 */
  {
    { "vgatherpf0qps",  { MVexVSIBDQWpX }, 0 },
    { "vgatherpf0qpd",  { MVexVSIBQWpX }, 0 },
  },
  /* EVEX_W_0F38C7_R_2_P_2 */
  {
    { "vgatherpf1qps",  { MVexVSIBDQWpX }, 0 },
    { "vgatherpf1qpd",  { MVexVSIBQWpX }, 0 },
  },
  /* EVEX_W_0F38C7_R_5_P_2 */
  {
    { "vscatterpf0qps",  { MVexVSIBDQWpX }, 0 },
    { "vscatterpf0qpd",  { MVexVSIBQWpX }, 0 },
  },
  /* EVEX_W_0F38C7_R_6_P_2 */
  {
    { "vscatterpf1qps",  { MVexVSIBDQWpX }, 0 },
    { "vscatterpf1qpd",  { MVexVSIBQWpX }, 0 },
  },
  /* EVEX_W_0F3A00_P_2 */
  {
    { Bad_Opcode },
    { "vpermq",	{ XM, EXx, Ib }, 0 },
  },
  /* EVEX_W_0F3A01_P_2 */
  {
    { Bad_Opcode },
    { "vpermpd",	{ XM, EXx, Ib }, 0 },
  },
  /* EVEX_W_0F3A04_P_2 */
  {
    { "vpermilps",	{ XM, EXx, Ib }, 0 },
  },
  /* EVEX_W_0F3A05_P_2 */
  {
    { Bad_Opcode },
    { "vpermilpd",	{ XM, EXx, Ib }, 0 },
  },
  /* EVEX_W_0F3A08_P_2 */
  {
    { "vrndscaleps",	{ XM, EXx, EXxEVexS, Ib }, 0 },
  },
  /* EVEX_W_0F3A09_P_2 */
  {
    { Bad_Opcode },
    { "vrndscalepd",	{ XM, EXx, EXxEVexS, Ib }, 0 },
  },
  /* EVEX_W_0F3A0A_P_2 */
  {
    { "vrndscaless",	{ XMScalar, VexScalar, EXxmm_md, EXxEVexS, Ib }, 0 },
  },
  /* EVEX_W_0F3A0B_P_2 */
  {
    { Bad_Opcode },
    { "vrndscalesd",	{ XMScalar, VexScalar, EXxmm_mq, EXxEVexS, Ib }, 0 },
  },
  /* EVEX_W_0F3A18_P_2 */
  {
    { "vinsertf32x4",	{ XM, Vex, EXxmm, Ib }, 0 },
    { "vinsertf64x2",	{ XM, Vex, EXxmm, Ib }, 0 },
  },
  /* EVEX_W_0F3A19_P_2 */
  {
    { "vextractf32x4",	{ EXxmm, XM, Ib }, 0 },
    { "vextractf64x2",	{ EXxmm, XM, Ib }, 0 },
  },
  /* EVEX_W_0F3A1A_P_2 */
  {
    { "vinsertf32x8",	{ XM, Vex, EXxmmq, Ib }, 0 },
    { "vinsertf64x4",	{ XM, Vex, EXxmmq, Ib }, 0 },
  },
  /* EVEX_W_0F3A1B_P_2 */
  {
    { "vextractf32x8",	{ EXxmmq, XM, Ib }, 0 },
    { "vextractf64x4",	{ EXxmmq, XM, Ib }, 0 },
  },
  /* EVEX_W_0F3A1D_P_2 */
  {
    { "vcvtps2ph",	{ EXxmmq, XM, EXxEVexS, Ib }, 0 },
  },
  /* EVEX_W_0F3A21_P_2 */
  {
    { "vinsertps",	{ XMM, Vex, EXxmm_md, Ib }, 0 },
  },
  /* EVEX_W_0F3A23_P_2 */
  {
    { "vshuff32x4",	{ XM, Vex, EXx, Ib }, 0 },
    { "vshuff64x2",	{ XM, Vex, EXx, Ib }, 0 },
  },
  /* EVEX_W_0F3A38_P_2 */
  {
    { "vinserti32x4",	{ XM, Vex, EXxmm, Ib }, 0 },
    { "vinserti64x2",	{ XM, Vex, EXxmm, Ib }, 0 },
  },
  /* EVEX_W_0F3A39_P_2 */
  {
    { "vextracti32x4",	{ EXxmm, XM, Ib }, 0 },
    { "vextracti64x2",	{ EXxmm, XM, Ib }, 0 },
  },
  /* EVEX_W_0F3A3A_P_2 */
  {
    { "vinserti32x8",	{ XM, Vex, EXxmmq, Ib }, 0 },
    { "vinserti64x4",	{ XM, Vex, EXxmmq, Ib }, 0 },
  },
  /* EVEX_W_0F3A3B_P_2 */
  {
    { "vextracti32x8",	{ EXxmmq, XM, Ib }, 0 },
    { "vextracti64x4",	{ EXxmmq, XM, Ib }, 0 },
  },
  /* EVEX_W_0F3A3E_P_2 */
  {
    { "vpcmpub",	{ XMask, Vex, EXx, VPCMP }, 0 },
    { "vpcmpuw",	{ XMask, Vex, EXx, VPCMP }, 0 },
  },
  /* EVEX_W_0F3A3F_P_2 */
  {
    { "vpcmpb",	{ XMask, Vex, EXx, VPCMP }, 0 },
    { "vpcmpw",	{ XMask, Vex, EXx, VPCMP }, 0 },
  },
  /* EVEX_W_0F3A42_P_2 */
  {
    { "vdbpsadbw",	{ XM, Vex, EXx, Ib }, 0 },
  },
  /* EVEX_W_0F3A43_P_2 */
  {
    { "vshufi32x4",	{ XM, Vex, EXx, Ib }, 0 },
    { "vshufi64x2",	{ XM, Vex, EXx, Ib }, 0 },
  },
  /* EVEX_W_0F3A50_P_2 */
  {
    { "vrangeps",	{ XM, Vex, EXx, EXxEVexS, Ib }, 0 },
    { "vrangepd",	{ XM, Vex, EXx, EXxEVexS, Ib }, 0 },
  },
  /* EVEX_W_0F3A51_P_2 */
  {
    { "vrangess",	{ XMScalar, VexScalar, EXxmm_md, EXxEVexS, Ib }, 0 },
    { "vrangesd",	{ XMScalar, VexScalar, EXxmm_mq, EXxEVexS, Ib }, 0 },
  },
  /* EVEX_W_0F3A56_P_2 */
  {
    { "vreduceps",	{ XM, EXx, EXxEVexS, Ib }, 0 },
    { "vreducepd",	{ XM, EXx, EXxEVexS, Ib }, 0 },
  },
  /* EVEX_W_0F3A57_P_2 */
  {
    { "vreducess",	{ XMScalar, VexScalar, EXxmm_md, EXxEVexS, Ib }, 0 },
    { "vreducesd",	{ XMScalar, VexScalar, EXxmm_mq, EXxEVexS, Ib }, 0 },
  },
  /* EVEX_W_0F3A66_P_2 */
  {
    { "vfpclassps%XZ",	{ XMask, EXx, Ib }, 0 },
    { "vfpclasspd%XZ",	{ XMask, EXx, Ib }, 0 },
  },
  /* EVEX_W_0F3A67_P_2 */
  {
    { "vfpclassss",	{ XMask, EXxmm_md, Ib }, 0 },
    { "vfpclasssd",	{ XMask, EXxmm_mq, Ib }, 0 },
  },
  /* EVEX_W_0F3A70_P_2 */
  {
    { Bad_Opcode },
    { "vpshldw",   { XM, Vex, EXx, Ib }, 0 },
  },
  /* EVEX_W_0F3A71_P_2 */
  {
    { "vpshldd",   { XM, Vex, EXx, Ib }, 0 },
    { "vpshldq",   { XM, Vex, EXx, Ib }, 0 },
  },
  /* EVEX_W_0F3A72_P_2 */
  {
    { Bad_Opcode },
    { "vpshrdw",   { XM, Vex, EXx, Ib }, 0 },
  },
  /* EVEX_W_0F3A73_P_2 */
  {
    { "vpshrdd",   { XM, Vex, EXx, Ib }, 0 },
    { "vpshrdq",   { XM, Vex, EXx, Ib }, 0 },
  },
  /* EVEX_W_0F3ACE_P_2 */
  {
    { Bad_Opcode },
    { "vgf2p8affineqb",    { XM, Vex, EXx, Ib }, 0 },
  },
  /* EVEX_W_0F3ACF_P_2 */
  {
    { Bad_Opcode },
    { "vgf2p8affineinvqb", { XM, Vex, EXx, Ib }, 0 },
  },
#endif /* NEED_VEX_W_TABLE */
#ifdef NEED_MOD_TABLE
  {
    /* MOD_EVEX_0F10_PREFIX_1 */
    { VEX_W_TABLE (EVEX_W_0F10_P_1_M_0) },
    { VEX_W_TABLE (EVEX_W_0F10_P_1_M_1) },
  },
  {
    /* MOD_EVEX_0F10_PREFIX_3 */
    { VEX_W_TABLE (EVEX_W_0F10_P_3_M_0) },
    { VEX_W_TABLE (EVEX_W_0F10_P_3_M_1) },
  },
  {
    /* MOD_EVEX_0F11_PREFIX_1 */
    { VEX_W_TABLE (EVEX_W_0F11_P_1_M_0) },
    { VEX_W_TABLE (EVEX_W_0F11_P_1_M_1) },
  },
  {
    /* MOD_EVEX_0F11_PREFIX_3 */
    { VEX_W_TABLE (EVEX_W_0F11_P_3_M_0) },
    { VEX_W_TABLE (EVEX_W_0F11_P_3_M_1) },
  },
  {
    /* MOD_EVEX_0F12_PREFIX_0 */
    { VEX_W_TABLE (EVEX_W_0F12_P_0_M_0) },
    { VEX_W_TABLE (EVEX_W_0F12_P_0_M_1) },
  },
  {
    /* MOD_EVEX_0F16_PREFIX_0 */
    { VEX_W_TABLE (EVEX_W_0F16_P_0_M_0) },
    { VEX_W_TABLE (EVEX_W_0F16_P_0_M_1) },
  },
  {
    /* MOD_EVEX_0F38C6_REG_1 */
    { PREFIX_TABLE (PREFIX_EVEX_0F38C6_REG_1) },
  },
  {
    /* MOD_EVEX_0F38C6_REG_2 */
    { PREFIX_TABLE (PREFIX_EVEX_0F38C6_REG_2) },
  },
  {
    /* MOD_EVEX_0F38C6_REG_5 */
    { PREFIX_TABLE (PREFIX_EVEX_0F38C6_REG_5) },
  },
  {
    /* MOD_EVEX_0F38C6_REG_6 */
    { PREFIX_TABLE (PREFIX_EVEX_0F38C6_REG_6) },
  },
  {
    /* MOD_EVEX_0F38C7_REG_1 */
    { PREFIX_TABLE (PREFIX_EVEX_0F38C7_REG_1) },
  },
  {
    /* MOD_EVEX_0F38C7_REG_2 */
    { PREFIX_TABLE (PREFIX_EVEX_0F38C7_REG_2) },
  },
  {
    /* MOD_EVEX_0F38C7_REG_5 */
    { PREFIX_TABLE (PREFIX_EVEX_0F38C7_REG_5) },
  },
  {
    /* MOD_EVEX_0F38C7_REG_6 */
    { PREFIX_TABLE (PREFIX_EVEX_0F38C7_REG_6) },
  },
#endif /* NEED_MOD_TABLE */

#ifdef NEED_EVEX_LEN_TABLE
  /* EVEX_LEN_0F6E_P_2 */
  {
    { "vmovK",	{ XMScalar, Edq }, 0 },
  },

  /* EVEX_LEN_0F7E_P_1 */
  {
    { VEX_W_TABLE (EVEX_W_0F7E_P_1) },
  },

  /* EVEX_LEN_0F7E_P_2 */
  {
    { "vmovK",	{ Edq, XMScalar }, 0 },
  },

  /* EVEX_LEN_0FD6_P_2 */
  {
    { VEX_W_TABLE (EVEX_W_0FD6_P_2) },
  },

#endif /* NEED_EVEX_LEN_TABLE */
