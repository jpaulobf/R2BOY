#include "Window/NtscShaderPipeline.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace R2BOY::Window;
int main(int argc, char **argv)
{
    try
    {
        if (argc != 2)
            throw std::runtime_error("Expected preset path");
        NtscShaderPipeline pipeline;
        std::string error;
        if (!pipeline.initialize(argv[1], error))
            throw std::runtime_error(error);
        std::vector<uint32_t> input(160 * 144), first, second, resized;
        for (int y = 0; y < 144; ++y)
            for (int x = 0; x < 160; ++x)
                input[y * 160 + x] = ((x / 8 + y / 8) % 2) ? 0xffff4000 : 0xff0080ff;
        if (!pipeline.render(input.data(), 160, {160, 144}, {320, 288}, first, error))
            throw std::runtime_error(error);
        if (!pipeline.render(input.data(), 160, {160, 144}, {320, 288}, second, error))
            throw std::runtime_error(error);
        if (first.size() != 320 * 288 || first == second ||
            std::all_of(first.begin(), first.end(), [&](uint32_t p)
                        { return p == first.front(); }))
            throw std::runtime_error("Output is uniform, wrong size, or frame modulation is absent");
        if (!pipeline.render(input.data() + 8 * 160 + 8, 160, {144, 128}, {360, 336}, resized, error))
            throw std::runtime_error(error);
        if (resized.size() != 360 * 336)
            throw std::runtime_error("Resize failed");
        std::cout << "NTSC GPU passes passed\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}