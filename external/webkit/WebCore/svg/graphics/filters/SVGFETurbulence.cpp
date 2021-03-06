

#include "config.h"

#if ENABLE(SVG) && ENABLE(FILTERS)
#include "SVGFETurbulence.h"
#include "SVGRenderTreeAsText.h"
#include "Filter.h"

namespace WebCore {

FETurbulence::FETurbulence(TurbulanceType type, const float& baseFrequencyX, const float& baseFrequencyY,
    const int& numOctaves, const float& seed, bool stitchTiles)
    : FilterEffect()
    , m_type(type)
    , m_baseFrequencyX(baseFrequencyX)
    , m_baseFrequencyY(baseFrequencyY)
    , m_numOctaves(numOctaves)
    , m_seed(seed)
    , m_stitchTiles(stitchTiles)
{
}

PassRefPtr<FETurbulence> FETurbulence::create(TurbulanceType type, const float& baseFrequencyX, const float& baseFrequencyY,
    const int& numOctaves, const float& seed, bool stitchTiles)
{
    return adoptRef(new FETurbulence(type, baseFrequencyX, baseFrequencyY, numOctaves, seed, stitchTiles));
}

TurbulanceType FETurbulence::type() const
{
    return m_type;
}

void FETurbulence::setType(TurbulanceType type)
{
    m_type = type;
}

float FETurbulence::baseFrequencyY() const
{
    return m_baseFrequencyY;
}

void FETurbulence::setBaseFrequencyY(float baseFrequencyY)
{
    m_baseFrequencyY = baseFrequencyY;
}

float FETurbulence::baseFrequencyX() const
{
    return m_baseFrequencyX;
}

void FETurbulence::setBaseFrequencyX(float baseFrequencyX)
{
       m_baseFrequencyX = baseFrequencyX;
}

float FETurbulence::seed() const
{
    return m_seed; 
}

void FETurbulence::setSeed(float seed)
{
    m_seed = seed;
}

int FETurbulence::numOctaves() const
{
    return m_numOctaves;
}

void FETurbulence::setNumOctaves(bool numOctaves)
{
    m_numOctaves = numOctaves;
}

bool FETurbulence::stitchTiles() const
{
    return m_stitchTiles;
}

void FETurbulence::setStitchTiles(bool stitch)
{
    m_stitchTiles = stitch;
}

void FETurbulence::apply(Filter*)
{
}

void FETurbulence::dump()
{
}

static TextStream& operator<<(TextStream& ts, TurbulanceType t)
{
    switch (t)
    {
        case FETURBULENCE_TYPE_UNKNOWN:
            ts << "UNKNOWN"; break;
        case FETURBULENCE_TYPE_TURBULENCE:
            ts << "TURBULANCE"; break;
        case FETURBULENCE_TYPE_FRACTALNOISE:
            ts << "NOISE"; break;
    }
    return ts;
}

TextStream& FETurbulence::externalRepresentation(TextStream& ts) const
{
    ts << "[type=TURBULENCE] ";
    FilterEffect::externalRepresentation(ts);
    ts << " [turbulence type=" << type() << "]"
        << " [base frequency x=" << baseFrequencyX() << " y=" << baseFrequencyY() << "]"
        << " [seed=" << seed() << "]"
        << " [num octaves=" << numOctaves() << "]"
        << " [stitch tiles=" << stitchTiles() << "]";

    return ts;
}

} // namespace WebCore

#endif // ENABLE(SVG) && ENABLE(FILTERS)
