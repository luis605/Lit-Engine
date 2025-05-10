/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef BRDF_HPP
#define BRDF_HPP

#include <raylib.h>

struct BRDF {
public:
    Texture2D m_brdfLut;

public:
    void GenLUT();
};

extern BRDF brdf;

#endif // BRDF_HPP