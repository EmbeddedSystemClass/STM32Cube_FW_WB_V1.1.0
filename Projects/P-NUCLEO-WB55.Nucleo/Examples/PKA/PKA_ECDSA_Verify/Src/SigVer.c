/**
  ******************************************************************************
  * @file    PKA/PKA_ECDSA_Verify/Src/SigVer.c
  * @author  MCD Application Team
  * @brief   This file contains reference buffers from 
  *          NIST Cryptographic Algorithm Validation Program (CAVP).
  *          (http://csrc.nist.gov/groups/STM/cavp/)
  *          1 test vector is extracted to demonstrate PKA capability to
  *          verify a signature using ECDSA (Elliptic Curve Digital Signature Algorithm) 
  *          signature verification function principle.
  *          It is adapted from SigVer.rsp section [P-256,SHA-256] available under
  *          http://csrc.nist.gov/groups/STM/cavp/documents/dss/186-3ecdsatestvectors.zip
  *          and provided in the same directory for reference.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2019 STMicroelectronics. 
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the 
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
  
/* Includes ------------------------------------------------------------------*/
#include "main.h"
/*
  Adapted from
  [P-256,SHA-256]
  Msg = e1130af6a38ccb412a9c8d13e15dbfc9e69a16385af3c3f1e5da954fd5e7c45fd75e2b8c36699228e92840c0562fbf3772f07e17f1add56588dd45f7450e1217ad239922dd9c32695dc71ff2424ca0dec1321aa47064a044b7fe3c2b97d03ce470a592304c5ef21eed9f93da56bb232d1eeb0035f9bf0dfafdcc4606272b20a3
  Qx = e424dc61d4bb3cb7ef4344a7f8957a0c5134e16f7a67c074f82e6e12f49abf3c
  Qy = 970eed7aa2bc48651545949de1dddaf0127e5965ac85d1243d6f60e7dfaee927
  R = bf96b99aa49c705c910be33142017c642ff540c76349b9dab72f981fd9347f4f
  S = 17c55095819089c2e03b9cd415abdf12444e323075d98f31920b9e0f57ec871c
  Result = P (0 )
  
  The hash of Msg is not part of PKA processing. It is provided directly in SigVer_Hash_Msg.
  For reference, this buffer is created using SHA-256 hash with Msg as input in Hex bytes format.
*/
const uint8_t SigVer_Msg[] = {
 0xe1, 0x13, 0x0a, 0xf6, 0xa3, 0x8c, 0xcb, 0x41, 0x2a, 0x9c, 0x8d, 0x13, 0xe1, 0x5d, 0xbf, 0xc9,
 0xe6, 0x9a, 0x16, 0x38, 0x5a, 0xf3, 0xc3, 0xf1, 0xe5, 0xda, 0x95, 0x4f, 0xd5, 0xe7, 0xc4, 0x5f,
 0xd7, 0x5e, 0x2b, 0x8c, 0x36, 0x69, 0x92, 0x28, 0xe9, 0x28, 0x40, 0xc0, 0x56, 0x2f, 0xbf, 0x37,
 0x72, 0xf0, 0x7e, 0x17, 0xf1, 0xad, 0xd5, 0x65, 0x88, 0xdd, 0x45, 0xf7, 0x45, 0x0e, 0x12, 0x17,
 0xad, 0x23, 0x99, 0x22, 0xdd, 0x9c, 0x32, 0x69, 0x5d, 0xc7, 0x1f, 0xf2, 0x42, 0x4c, 0xa0, 0xde,
 0xc1, 0x32, 0x1a, 0xa4, 0x70, 0x64, 0xa0, 0x44, 0xb7, 0xfe, 0x3c, 0x2b, 0x97, 0xd0, 0x3c, 0xe4,
 0x70, 0xa5, 0x92, 0x30, 0x4c, 0x5e, 0xf2, 0x1e, 0xed, 0x9f, 0x93, 0xda, 0x56, 0xbb, 0x23, 0x2d,
 0x1e, 0xeb, 0x00, 0x35, 0xf9, 0xbf, 0x0d, 0xfa, 0xfd, 0xcc, 0x46, 0x06, 0x27, 0x2b, 0x20, 0xa3
};
const uint32_t SigVer_Msg_len = 128;

const uint8_t SigVer_Hash_Msg[] = {
 0xd1, 0xb8, 0xef, 0x21, 0xeb, 0x41, 0x82, 0xee, 0x27, 0x06, 0x38, 0x06, 0x10, 0x63, 0xa3, 0xf3,
 0xc1, 0x6c, 0x11, 0x4e, 0x33, 0x93, 0x7f, 0x69, 0xfb, 0x23, 0x2c, 0xc8, 0x33, 0x96, 0x5a, 0x94
};

/* Add a false hash message by corrupting the first byte of SigVer_Hash_Msg */
const uint8_t SigVer_Hash_Msg_False[] = {
 0x00, 0xb8, 0xef, 0x21, 0xeb, 0x41, 0x82, 0xee, 0x27, 0x06, 0x38, 0x06, 0x10, 0x63, 0xa3, 0xf3,
 0xc1, 0x6c, 0x11, 0x4e, 0x33, 0x93, 0x7f, 0x69, 0xfb, 0x23, 0x2c, 0xc8, 0x33, 0x96, 0x5a, 0x94
};

const uint32_t SigVer_Hash_Msg_len = 32;

const uint8_t SigVer_Qx[] = {
 0xe4, 0x24, 0xdc, 0x61, 0xd4, 0xbb, 0x3c, 0xb7, 0xef, 0x43, 0x44, 0xa7, 0xf8, 0x95, 0x7a, 0x0c,
 0x51, 0x34, 0xe1, 0x6f, 0x7a, 0x67, 0xc0, 0x74, 0xf8, 0x2e, 0x6e, 0x12, 0xf4, 0x9a, 0xbf, 0x3c
};
const uint32_t SigVer_Qx_len = 32;

const uint8_t SigVer_Qy[] = {
 0x97, 0x0e, 0xed, 0x7a, 0xa2, 0xbc, 0x48, 0x65, 0x15, 0x45, 0x94, 0x9d, 0xe1, 0xdd, 0xda, 0xf0,
 0x12, 0x7e, 0x59, 0x65, 0xac, 0x85, 0xd1, 0x24, 0x3d, 0x6f, 0x60, 0xe7, 0xdf, 0xae, 0xe9, 0x27
};
const uint32_t SigVer_Qy_len = 32;

const uint8_t SigVer_R[] = {
 0xbf, 0x96, 0xb9, 0x9a, 0xa4, 0x9c, 0x70, 0x5c, 0x91, 0x0b, 0xe3, 0x31, 0x42, 0x01, 0x7c, 0x64,
 0x2f, 0xf5, 0x40, 0xc7, 0x63, 0x49, 0xb9, 0xda, 0xb7, 0x2f, 0x98, 0x1f, 0xd9, 0x34, 0x7f, 0x4f
};
const uint32_t SigVer_R_len = 32;

const uint8_t SigVer_S[] = {
 0x17, 0xc5, 0x50, 0x95, 0x81, 0x90, 0x89, 0xc2, 0xe0, 0x3b, 0x9c, 0xd4, 0x15, 0xab, 0xdf, 0x12,
 0x44, 0x4e, 0x32, 0x30, 0x75, 0xd9, 0x8f, 0x31, 0x92, 0x0b, 0x9e, 0x0f, 0x57, 0xec, 0x87, 0x1c
};
const uint32_t SigVer_S_len = 32;

const uint32_t SigVer_Result = SET;

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
