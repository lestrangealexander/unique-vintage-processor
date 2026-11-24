// Unique Vintage Processor — knots of the auto-gain correction for the FILTER low-pass edge
#pragma once

namespace uvp::agc
{
constexpr int kLpfSteps = 7;
constexpr float kLpfHz[kLpfSteps] = { 2000.0f, 3000.0f, 4000.0f, 6000.0f, 8000.0f, 12000.0f, 16000.0f };
constexpr int kLpfAmountSteps = 8;
constexpr float kLpfAmounts[kLpfAmountSteps] = { 0.0f, 2.0f, 4.0f, 6.0f, 8.0f, 9.0f, 10.0f, 11.0f };
} // namespace uvp::agc
