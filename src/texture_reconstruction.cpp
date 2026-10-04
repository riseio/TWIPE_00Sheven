#include "texture_reconstruction.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace twine::textures {
namespace {
using Bytes = std::vector<uint8_t>;
void put(Bytes& bytes, size_t offset, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(offset+i) = uint8_t(value >> (8*i));
}
void validate(const Image& image) {
    if (!image.width || !image.height || image.width > 4096 || image.height > 4096 ||
        image.rgba.size() != size_t(image.width) * image.height * 4)
        throw std::runtime_error("Invalid reconstruction image dimensions/payload");
}
float slope(float a, float b) {
    return a * b <= 0 ? 0 : 2 * a * b / (a + b);
}
float interpolate(float a, float b, float c, float d, float t) {
    const float t2 = t*t, t3 = t2*t;
    const float result = (2*t3-3*t2+1)*b + (t3-2*t2+t)*slope(b-a,c-b) +
        (-2*t3+3*t2)*c + (t3-t2)*slope(c-b,d-c);
    return std::clamp(result, std::min(b,c), std::max(b,c));
}
Image mip(const Image& src) {
    Image out{std::max(1U,src.width/2),std::max(1U,src.height/2),{}};
    out.rgba.resize(size_t(out.width)*out.height*4);

    for (uint32_t y=0;y<out.height;++y) for (uint32_t x=0;x<out.width;++x) {
        const uint32_t x0=x*src.width, x1=(x+1)*src.width;
        const uint32_t y0=y*src.height, y1=(y+1)*src.height;
        std::array<uint64_t,4> sum{};
        for(uint32_t sy=y0/out.height;sy<(y1+out.height-1)/out.height;++sy)
            for(uint32_t sx=x0/out.width;sx<(x1+out.width-1)/out.width;++sx) {
                const uint64_t weight=(std::min(x1,(sx+1)*out.width)-std::max(x0,sx*out.width))*
                    uint64_t(std::min(y1,(sy+1)*out.height)-std::max(y0,sy*out.height));
                for(unsigned c=0;c<4;++c) sum[c]+=src.rgba[(size_t(sy)*src.width+sx)*4+c]*weight;
            }
        const uint64_t total=uint64_t(src.width)*src.height;
        for(unsigned c=0;c<4;++c) out.rgba[(size_t(y)*out.width+x)*4+c]=uint8_t((sum[c]+total/2)/total);
    }
    return out;
}
}

Image reconstruct(const Image& src, uint32_t scale) {
    validate(src);
    if(!scale || scale>16 || src.width>4096/scale || src.height>4096/scale)
        throw std::runtime_error("Invalid reconstruction scale");
    Image out{src.width*scale,src.height*scale,{}};
    out.rgba.resize(size_t(out.width)*out.height*4);

    std::vector<float> horizontal(size_t(src.height)*out.width*4);
    for(uint32_t y=0;y<src.height;++y) for(uint32_t x=0;x<out.width;++x) {
        const float u=(float(x)+0.5f)/scale-0.5f;
        const int ix=int(std::floor(u));
        for(unsigned c=0;c<4;++c) {
            std::array<float,4> p;
            for(int col=-1;col<=2;++col) {
                const int sx=std::clamp(ix+col,0,int(src.width)-1);
                p[col+1]=src.rgba[(size_t(y)*src.width+sx)*4+c];
            }
            horizontal[(size_t(y)*out.width+x)*4+c]=interpolate(p[0],p[1],p[2],p[3],u-ix);
        }
    }
    for(uint32_t y=0;y<out.height;++y) for(uint32_t x=0;x<out.width;++x) {
        const float v=(float(y)+0.5f)/scale-0.5f;
        const int iy=int(std::floor(v));
        for(unsigned c=0;c<4;++c) {
            std::array<float,4> rows;
            for(int row=-1;row<=2;++row) {
                const int sy=std::clamp(iy+row,0,int(src.height)-1);
                rows[row+1]=horizontal[(size_t(sy)*out.width+x)*4+c];
            }
            out.rgba[(size_t(y)*out.width+x)*4+c]=uint8_t(std::lround(
                interpolate(rows[0],rows[1],rows[2],rows[3],v-iy)));
        }
    }
    return out;
}

Bytes make_dds(const Image& image) {
    validate(image);
    uint32_t levels=1;
    for(uint32_t w=image.width,h=image.height;w>1||h>1;++levels) {w=std::max(1U,w/2);h=std::max(1U,h/2);}
    Bytes out(148,0);
    put(out,0,0x20534444);put(out,4,124);put(out,8,0x2100F);
    put(out,12,image.height);put(out,16,image.width);put(out,20,image.width*4);put(out,28,levels);
    put(out,76,32);put(out,80,4);put(out,84,0x30315844);put(out,108,0x401008);
    put(out,128,28);put(out,132,3);put(out,140,1);
    Image level=image;
    for(;;) {
        out.insert(out.end(),level.rgba.begin(),level.rgba.end());
        if(level.width==1 && level.height==1) break;
        level=mip(level);
    }
    return out;
}

}
