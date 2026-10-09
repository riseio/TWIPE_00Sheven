// MMPX by Morgan McGuire and Mara Gagiu, copyright 2020, MIT licensed.

#pragma once
#include <array>
namespace twine::fonts::detail {
static inline bool all_eq2(int B, int A0, int A1)
{
    return ((B ^ A0) | (B ^ A1)) == 0;
}

static inline bool all_eq3(int B, int A0, int A1, int A2)
{
    return ((B ^ A0) | (B ^ A1) | (B ^ A2)) == 0;
}

static inline bool all_eq4(int B, int A0, int A1, int A2, int A3)
{
    return ((B ^ A0) | (B ^ A1) | (B ^ A2) | (B ^ A3)) == 0;
}

static inline bool any_eq3(int B, int A0, int A1, int A2)
{
    return B == A0 || B == A1 || B == A2;
}

static inline bool none_eq2(int B, int A0, int A1)
{
    return (B != A0) && (B != A1);
}

static inline bool none_eq4(int B, int A0, int A1, int A2, int A3)
{
    return B != A0 && B != A1 && B != A2 && B != A3;
}

template<class Sample>
std::array<int, 4> magnify(int srcX, int srcY, const Sample& sample) {
    const int A = sample(srcX - 1, srcY - 1), B = sample(srcX, srcY - 1), C = sample(srcX + 1, srcY - 1);
    const int D = sample(srcX - 1, srcY), E = sample(srcX, srcY), F = sample(srcX + 1, srcY);
    const int G = sample(srcX - 1, srcY + 1), H = sample(srcX, srcY + 1), I = sample(srcX + 1, srcY + 1);
    const int Q = sample(srcX - 2, srcY), R = sample(srcX + 2, srcY);

    int J = E, K = E, L = E, M = E;

    if (((A ^ E) | (B ^ E) | (C ^ E) | (D ^ E) | (F ^ E) | (G ^ E) | (H ^ E) | (I ^ E)) != 0)
    {
        const int P = sample(srcX, srcY - 2), S = sample(srcX, srcY + 2);
        const int Bl = B, Dl = D, El = E, Fl = F, Hl = H;

        {
            if ((D == B && D != H && D != F) && (El >= Dl || E == A) && any_eq3(E, A, C, G) && ((El < Dl) || A != D || E != P || E != Q)) J = D;
            if ((B == F && B != D && B != H) && (El >= Bl || E == C) && any_eq3(E, A, C, I) && ((El < Bl) || C != B || E != P || E != R)) K = B;
            if ((H == D && H != F && H != B) && (El >= Hl || E == G) && any_eq3(E, A, G, I) && ((El < Hl) || G != H || E != S || E != Q)) L = H;
            if ((F == H && F != B && F != D) && (El >= Fl || E == I) && any_eq3(E, C, G, I) && ((El < Fl) || I != H || E != R || E != S)) M = F;
        }

        {
            if ((E != F && all_eq4(E, C, I, D, Q) && all_eq2(F, B, H)) && (F != sample(srcX + 3, srcY))) K = M = F;
            if ((E != D && all_eq4(E, A, G, F, R) && all_eq2(D, B, H)) && (D != sample(srcX - 3, srcY))) J = L = D;
            if ((E != H && all_eq4(E, G, I, B, P) && all_eq2(H, D, F)) && (H != sample(srcX, srcY + 3))) L = M = H;
            if ((E != B && all_eq4(E, A, C, H, S) && all_eq2(B, D, F)) && (B != sample(srcX, srcY - 3))) J = K = B;
            if (Bl < El && all_eq4(E, G, H, I, S) && none_eq4(E, A, D, C, F)) J = K = B;
            if (Hl < El && all_eq4(E, A, B, C, P) && none_eq4(E, D, G, I, F)) L = M = H;
            if (Fl < El && all_eq4(E, A, D, G, Q) && none_eq4(E, B, C, I, H)) K = M = F;
            if (Dl < El && all_eq4(E, C, F, I, R) && none_eq4(E, B, A, G, H)) J = L = D;
        }

        {
            if (H != B)
            {
                if (H != A && H != E && H != C)
                {
                    if (all_eq3(H, G, F, R) && none_eq2(H, D, sample(srcX + 2, srcY - 1))) L = M;
                    if (all_eq3(H, I, D, Q) && none_eq2(H, F, sample(srcX - 2, srcY - 1))) M = L;
                }

                if (B != I && B != G && B != E)
                {
                    if (all_eq3(B, A, F, R) && none_eq2(B, D, sample(srcX + 2, srcY + 1))) J = K;
                    if (all_eq3(B, C, D, Q) && none_eq2(B, F, sample(srcX - 2, srcY + 1))) K = J;
                }
            }

            if (F != D)
            {
                if (D != I && D != E && D != C)
                {
                    if (all_eq3(D, A, H, S) && none_eq2(D, B, sample(srcX + 1, srcY + 2))) J = L;
                    if (all_eq3(D, G, B, P) && none_eq2(D, H, sample(srcX + 1, srcY - 2))) L = J;
                }

                if (F != E && F != A && F != G)
                {
                    if (all_eq3(F, C, H, S) && none_eq2(F, B, sample(srcX - 1, srcY + 2))) K = M;
                    if (all_eq3(F, I, B, P) && none_eq2(F, H, sample(srcX - 1, srcY - 2))) M = K;
                }
            }
        }
    }

    return {J, K, L, M};
}
}
