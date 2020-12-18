#include "simulation.h"
#include <Eigen/Core>
#include <chrono>
#include <igl/opengl/glfw/Viewer.h>
#include <iostream>
#include <random>
int main() {
    std::vector<vec3> particles;
    std::condition_variable cv;
    {
        std::random_device rd;
        std::uniform_real_distribution<float> dist;
        for (float x = 0.3; x < 0.6; x += 0.02) {
            for (float z = 0.3; z < 0.6; z += 0.02) {
                for (float y = 0.03; y < 0.2; y += 0.02) {
                    particles.emplace_back(x, y, z);
                }
            }
        }
        // for(int i= 0;i < 4000;i++){
        //     particles.emplace_back(dist(rd), dist(rd), dist(rd));
        // }
    }
    Simulation sim(particles);
    bool flag = true;
    Eigen::MatrixXd P;
    P.resize(0, 3);
    std::mutex m;
    std::thread sim_thd([&] {
        std::unique_lock<std::mutex> lk(m, std::defer_lock);
        while (flag) {
            sim.run_step();
            lk.lock();
            P.resize(sim.buffers.num_particles, 3);
            for (size_t i = 0; i < sim.buffers.num_particles; i++) {
                auto p = sim.buffers.particle_position[i];
                // printf("%f %f %f\n", p.x, p.y, p.z);
                P.row(i) = Eigen::RowVector3d(p.x, p.y, p.z);
            }
            lk.unlock();
            cv.notify_one();
        }
    });
    using Viewer = igl::opengl::glfw::Viewer;
    igl::opengl::glfw::Viewer viewer;
    viewer.callback_post_draw = [&](Viewer &) -> bool {
        std::unique_lock<std::mutex> lk(m);
        if (std::cv_status::no_timeout == cv.wait_for(lk, std::chrono::milliseconds(16))) {
            viewer.data().point_size = 5;
            viewer.data().set_points(P, Eigen::RowVector3d(1, 1, 1));
        }
        return false;
    };
    viewer.core().is_animating = true;
    viewer.launch();
    flag = false;

    sim_thd.join();
}