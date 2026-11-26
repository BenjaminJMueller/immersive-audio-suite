// libmysofa inflates the compressed HDF5 chunks of a SOFA file through gunzip(). This
// provides it with JUCE's built-in zlib, so no separate zlib library is needed.
#include <juce_core/juce_core.h>

extern "C" int gunzip(int inlen, char* in, int* outlen, char* out) {
    if (in == nullptr || out == nullptr || outlen == nullptr || inlen < 0 || *outlen < 0) return -1;
    juce::MemoryInputStream src(in, (size_t)inlen, false);
    juce::GZIPDecompressorInputStream z(&src, false, juce::GZIPDecompressorInputStream::zlibFormat);
    int total = 0;
    while (total < *outlen) {
        const int got = z.read(out + total, *outlen - total);
        if (got <= 0) break;
        total += got;
    }
    if (total == 0 && *outlen > 0) return -1;
    *outlen = total;
    return 0;
}
