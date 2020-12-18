#pragma once
// #include <cuda.h>
// #include <cuda_runtime.h>
// #define GLM_FORCE_CUDA
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

using glm::ivec2;
using glm::ivec3;
using glm::vec2;
using glm::vec3;

// template <typename T, typename... Args>
// T *cuda_new(Args... args)
// {
//     T *p;
//     cudaMallocManaged(&p, sizeof(T));
//     new (p) T(args...);
//     return p;
// }

// template <typename T, typename... Args>
// T *cuda_new_array(size_t n, Args... args)
// {
//     T *p;
//     cudaMallocManaged(&p, sizeof(T) * n);
//     for (size_t i = 0; i < n; i++)
//     {
//         new (p + i) T(args...);
//     }
//     return p;
// }
#define CHECK(expr)                                                                                                    \
    [&] {                                                                                                              \
        if (!(expr)) {                                                                                                 \
            fprintf(stderr, #expr " failed at %s:%d\n", __FILE__, __LINE__);                                           \
        }                                                                                                              \
    }()
class Simulation {

    struct Cell {
        static constexpr size_t max_particles = 100;
        std::array<uint32_t, max_particles> particles;
        std::atomic<uint32_t> n_particles;
        Cell() : n_particles(0) {}
    };
    struct Neighbors {
        static constexpr size_t max_neighbors = 200;
        std::array<uint32_t, max_neighbors> neighbors;
        size_t n_neighbors = 0;
    };
    // if we want to port to cuda but not want to port a std::vector (since they simply don't have __device__ attached)
    // this is our best chance
    struct Buffers {
        // SOA for max locality
        std::unique_ptr<vec3[]> particle_position;
        std::unique_ptr<vec3[]> particle_velocity;
        std::unique_ptr<float[]> density;
        std::unique_ptr<vec3[]> dvdt;
        std::unique_ptr<float[]> drhodt;
        std::unique_ptr<float[]> P;
        std::unique_ptr<Cell[]> grid;
        std::unique_ptr<Neighbors[]> neighbors;
        size_t num_particles = 0;
    };
    struct Pointers {
        vec3 *particle_position = nullptr;
        vec3 *particle_velocity = nullptr;
        float *density = nullptr;
        vec3 *dvdt = nullptr;
        float *drhodt = nullptr;
        float *P = nullptr;
        Cell *grid = nullptr;
        Neighbors *neighbors = nullptr;
    };
    void init();
    float radius = 0.02f;
    float dh = radius * 1.3f;
    float c0 = 20;
    float rho0 = 1000;
    float gamma = 7;
    float kappa = 1.0;
    float alpha = 0.5;
    float dt = 0.0001;
    int size = 0;
    float mass = 0.0;
    ivec3 grid_size;
    uint32_t get_index_i(const ivec3 &p) const { return p.x + p.y * grid_size.x + p.z * grid_size.x * grid_size.y; }
    ivec3 get_cell(const vec3 &p) const {
        ivec3 ip = p * vec3(grid_size);
        ip = glm::max(glm::min(ip, grid_size - 1), ivec3(0));
        return ip;
    }
    uint32_t get_grid_index(const vec3 &p) const {
        auto ip = get_cell(p);
        return get_index_i(ip);
    }
    size_t num_particles = 4000;
    void build_grid();
    void find_neighbors();
    vec3 dvdt_momentum_term(size_t id);
    vec3 dvdt_viscosity_term(size_t id);
    vec3 dvdt_full(size_t id);
    float drhodt(size_t id);
    void naive_collison_handling();
    float P(size_t id);
    void run_step_euler();
    void run_step_adami();
  public:
    Buffers buffers;
    Pointers pointers;
    Simulation(const std::vector<vec3> &particles) : size(size), num_particles(particles.size()) {
        init();
        for (size_t i = 0; i < num_particles; i++) {
            pointers.particle_position[i] = particles[i];
        }
    }
    void run_step();
};