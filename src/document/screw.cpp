#include "screw.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace dune3d {

namespace {
// ISO 4017/4762/7045/7046-ish: head sizes for the standard diameters
struct Row {
    double d, pitch;
    double hex_s, hex_k;   // across flats, height
    double cap_dk, cap_s;  // head diameter, socket across flats (height = d)
    double pan_dk, pan_k;
};

constexpr std::array<Row, 7> rows = {{
        {3, .5, 5.5, 2, 5.5, 2.5, 5.6, 2.4},
        {4, .7, 7, 2.8, 7, 3, 8, 3.1},
        {5, .8, 8, 3.5, 8.5, 4, 9.5, 3.8},
        {6, 1, 10, 4, 10, 5, 12, 4.6},
        {8, 1.25, 13, 5.3, 13, 6, 16, 6},
        {10, 1.5, 16, 6.4, 16, 8, 20, 7.5},
        {12, 1.75, 18, 7.5, 18, 10, 24, 9},
}};

// nearest standard row, with sizes scaled to the wanted diameter
Row row_for(double d)
{
    const Row *best = &rows.front();
    for (const auto &r : rows) {
        if (std::abs(std::log(d / r.d)) < std::abs(std::log(d / best->d)))
            best = &r;
    }
    const double f = d / best->d;
    Row r = *best;
    r.d = d;
    r.pitch *= f;
    r.hex_s *= f;
    r.hex_k *= f;
    r.cap_dk *= f;
    r.cap_s *= f;
    r.pan_dk *= f;
    r.pan_k *= f;
    return r;
}

constexpr int max_thread_turns = 32; // longer shanks are plain above that, like partly threaded screws

double round2(double v)
{
    return std::round(v * 100) / 100;
}

json pt(double r, double z)
{
    return json::array({round2(r), round2(z)});
}

json hexagon(double across_flats)
{
    const double rad = across_flats / std::sqrt(3.);
    json o = json::array();
    for (int i = 0; i < 6; i++)
        o.push_back(pt(rad * std::cos(i * std::numbers::pi / 3), rad * std::sin(i * std::numbers::pi / 3)));
    return o;
}
} // namespace

double screw_pitch(double diameter)
{
    return row_for(diameter).pitch;
}

std::string screw_label(const ScrewParams &p)
{
    auto num = [](double v) {
        std::string s = std::to_string(round2(v));
        s.erase(s.find_last_not_of('0') + 1);
        if (s.back() == '.')
            s.pop_back();
        return s;
    };
    return "M" + num(p.diameter) + "x" + num(p.length);
}

json make_screw_recipe(const ScrewParams &p)
{
    const auto r = row_for(p.diameter);
    const double top = p.length; // under the head
    const double r_major = p.diameter / 2;
    const double pitch = r.pitch;
    const double depth = .6134 * pitch;
    const double r_minor = p.thread ? r_major - depth : r_major;

    const std::string name = "Screw " + screw_label(p);
    // the shank is a cylinder at the root of the thread (the whole pin when plain); it runs a
    // little into the head so the two overlap when joined
    json shank = {{"id", "screw"},
                  {"name", name},
                  {"shape", "cylinder"},
                  {"diameter", round2(2 * r_minor)},
                  {"height", round2(top + .5)},
                  {"material", "metal"},
                  {"color", "#b4b8bc"}};
    if (p.thread) {
        const double thread_length = std::min(top, max_thread_turns * pitch + 1.2 * pitch);
        shank["thread"] = {{"pitch", round2(pitch)}, {"depth", round2(depth)}, {"length", round2(thread_length)}};
    }

    json objects = json::array({shank});
    json combine = json::array();
    auto add_part = [&](json part, const char *op) {
        const std::string id = part.at("id");
        objects.push_back(std::move(part));
        combine.push_back(json{{"target", "screw"}, {"tool", id}, {"op", op}});
    };
    auto slot = [&](double R, double head_top, double depth_of_slot) {
        return json{{"id", "slot"},
                    {"shape", "box"},
                    {"width", round2(2 * R + 2)},
                    {"depth", round2(std::max(p.diameter * .2, .4))},
                    {"height", round2(depth_of_slot + 1)},
                    {"position", json::array({0, 0, round2(head_top - depth_of_slot)})}};
    };
    const json at_top = json::array({0, 0, round2(top)});

    switch (p.head) {
    case ScrewHead::HEX:
        add_part({{"id", "head"},
                  {"shape", "extrude"},
                  {"outline", hexagon(r.hex_s)},
                  {"height", round2(r.hex_k)},
                  {"position", at_top}},
                 "join");
        break;
    case ScrewHead::CAP: {
        add_part({{"id", "head"},
                  {"shape", "cylinder"},
                  {"diameter", round2(r.cap_dk)},
                  {"height", round2(p.diameter)},
                  {"position", at_top}},
                 "join");
        const double depth_of_socket = p.diameter * .6;
        add_part({{"id", "socket"},
                  {"shape", "extrude"},
                  {"outline", hexagon(r.cap_s)},
                  {"height", round2(depth_of_socket + 1)},
                  {"position", json::array({0, 0, round2(top + p.diameter - depth_of_socket)})}},
                 "subtract");
        break;
    }
    case ScrewHead::PAN: {
        const double R = r.pan_dk / 2, k = r.pan_k;
        // a quarter ellipse over the top (heights from the head's underside)
        json profile = json::array({pt(0, 0), pt(R, 0), pt(R, k * .55)});
        for (int i = 1; i < 6; i++) {
            const double a = i * std::numbers::pi / 12;
            profile.push_back(pt(R * std::cos(a), k * .55 + k * .45 * std::sin(a)));
        }
        profile.push_back(pt(0, k));
        add_part({{"id", "head"}, {"shape", "revolve"}, {"profile", profile}, {"position", at_top}}, "join");
        add_part(slot(R, top + k, std::max(k * .5, .5)), "subtract");
        break;
    }
    case ScrewHead::COUNTERSUNK: {
        // 90 degree cone, as wide as it is high
        const double R = p.diameter;
        const double k = R - r_minor;
        json profile = json::array({pt(0, 0), pt(r_minor, 0), pt(R, k), pt(0, k)});
        add_part({{"id", "head"}, {"shape", "revolve"}, {"profile", profile}, {"position", at_top}}, "join");
        add_part(slot(R, top + k, std::max(k * .6, .5)), "subtract");
        break;
    }
    }
    return {{"objects", objects}, {"combine", combine}};
}

} // namespace dune3d
