#include "reconstruction.h"
#include "simulation.h"
#include <Eigen/SVD>
#include <Eigen/StdVector>
#include <igl/PI.h>
#include <igl/copyleft/marching_cubes.h>
#include <iostream>
#include <tbb/parallel_for.h>

void reconstruct(Eigen::MatrixXd &V, Eigen::MatrixXi &F, const Eigen::MatrixXd &P, const Eigen::Vector3i &res,
                 const Eigen::VectorXd &mass, const Eigen::VectorXd &density, double h, double isovalue) {
    auto W_k = 10. / (7. * igl::PI);
    auto kernel_P = [&](double r) {
        auto q = r;
        auto res = 0.0;
        if (q <= 1.0)
            res = (1 - 1.5 * q * q + 0.75 * q * q * q);
        else if (q < 2.0) {
            auto two_m_q = 2 - q;
            res = 0.25 * two_m_q * two_m_q * two_m_q;
        }
        return res;
    };

    printf("P.rows() = %d\n", P.rows());
    using igl::copyleft::marching_cubes;
    // Eigen::Vector3d lower = Eigen::Vector3d::Zero() - Eigen::Vector3d::Constant(2 * h); // = P.colwise().minCoeff();
    // Eigen::Vector3d upper = Eigen::Vector3d::Ones() + Eigen::Vector3d::Constant(2 * h); // P.colwise().maxCoeff();
    Eigen::Vector3d lower = Eigen::Vector3d(P.colwise().minCoeff()) - Eigen::Vector3d::Constant(2 * h);
    Eigen::Vector3d upper = Eigen::Vector3d(P.colwise().maxCoeff()) + Eigen::Vector3d::Constant(2 * h);
    Eigen::Vector3d extent = upper - lower;
    Eigen::VectorXd S;
    Eigen::MatrixXd GV;
    S.resize(res.prod());
    GV.resize(res.prod(), 3);
    Eigen::Vector3d nn_cell_size(2 * h, 2 * h, 2 * h);
    Eigen::Vector3i nn_grid_size;
    nn_grid_size << extent[0] / nn_cell_size[0], extent[1] / nn_cell_size[1], extent[2] / nn_cell_size[2];
    Eigen::Vector3d grid_cell_size = extent.array() / (res - Eigen::Vector3i::Ones()).cast<double>().array();
    for (int i = 0; i < res.prod(); i++) {
        int x = i % res[0];
        int y = (i % (res[0] * res[1])) / res[0];
        int z = i / (res[0] * res[1]);
        Eigen::Vector3d p = Eigen::Vector3d(Eigen::Array3d(x, y, z) * grid_cell_size.array()) + lower;
        GV.row(i) = p;
    }
    // std::cout << GV << std::endl;
    struct Cell {
        std::vector<int> particles;
    };
    std::vector<Cell> grid(nn_grid_size.prod());
    std::vector<Eigen::Matrix3d, Eigen::aligned_allocator<Eigen::Matrix3d>> G(P.rows());
    std::vector<std::vector<int>> neighbors(P.rows());
    Eigen::MatrixXd X;
    X.resizeLike(P);
    // auto get_cell_index = [&](const Eigen::Vector3d &x) -> Eigen::Vector3i {
    //     Eigen::Vector3i ip =
    //         Eigen::Vector3i(((x - lower).array() / extent.array() * res.array().cast<double>()).cast<int>());
    //     for (int i = 0; i < 3; i++) {
    //         ip[i] = std::min(res[i] - 1, std::max(ip[i], 0));
    //     }
    //     return ip;
    // };
    // auto get_linear_index = [&](const Eigen::Vector3i &ip) { return ip[0] + ip[1] * res[0] + ip[2] * res[0] * res[1];
    // };
    auto get_nn_cell_index = [&](const Eigen::Vector3d &x) -> Eigen::Vector3i {
        Eigen::Vector3i ip = Eigen::Vector3i(((x - lower).array() / nn_cell_size.array()).cast<int>());
        for (int i = 0; i < 3; i++) {
            ip[i] = std::min(nn_grid_size[i] - 1, std::max(ip[i], 0));
        }
        return ip;
    };
    auto get_nn_linear_index = [&](const Eigen::Vector3i &ip) {
        return ip[0] + ip[1] * nn_grid_size[0] + ip[2] * nn_grid_size[0] * nn_grid_size[1];
    };
    for (int i = 0; i < P.rows(); i++) {
        Eigen::Vector3i ip = get_nn_cell_index(P.row(i));
        auto idx = get_nn_linear_index(ip);
        CHECK(idx < grid.size());
        // printf("%d\n", idx);
        grid[idx].particles.push_back(i);
    }
    for (int i = 0; i < P.rows(); i++) {

        Eigen::Vector3d p = P.row(i);
        Eigen::Vector3i ip = get_nn_cell_index(p);
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                for (int dz = -1; dz <= 1; dz++) {
                    Eigen::Vector3i cell_idx = ip + Eigen::Vector3i(dx, dy, dz);
                    if ((cell_idx.array() >= Eigen::Array3i::Zero()).all() &&
                        (cell_idx.array() < nn_grid_size.array()).all()) {
                        auto &cell = grid[get_nn_linear_index(cell_idx)];
                        for (auto &j : cell.particles) {
                            if (i == j)
                                continue;
                            Eigen::Vector3d q = P.row(j);
                            if ((p - q).norm() < 2 * h) {
                                neighbors[i].push_back(j);
                            }
                        }
                    }
                }
            }
        }
    }
    CHECK(neighbors.size() == P.rows());
    // for (int i = 0; i < P.rows(); i++) {
    tbb::parallel_for<int>(0, P.rows(), [&](int i) {
        Eigen::Vector3d xi = P.row(i);
        Eigen::Vector3d xiw(0, 0, 0);
        Eigen::Matrix3d C;
        Eigen::Vector3d x_bar(0.0, 0.0, 0.0);
        const auto lambda = 0.92;
        C.setZero();
        if (!neighbors.empty()) {
            double sum_w = 0.0;

            for (auto &j : neighbors[i]) {
                CHECK(j < P.rows());
                Eigen::Vector3d xj = P.row(j);
                const auto r = 2 * h;
                auto w = 0.0;
                if ((xi - xj).norm() <= r && (xi - xj).norm() > 0.0) {
                    w = 1.0 - std::pow((xi - xj).norm() / r, 3);
                }
                sum_w += w;
                xiw += w * xj;
            }
            xiw /= sum_w;
            x_bar = (1.0 - lambda) * xi + lambda * xiw;
            X.row(i) = x_bar;
        } else {
            X.row(i) = xi;
            xiw = xi;
        }
        {
            double sum_w = 0.0;
            for (auto &j : neighbors[i]) {
                Eigen::Vector3d xj = P.row(j);
                const auto r = 2 * h;
                auto w = 0.0;
                if ((xi - xj).norm() <= r && (xi - xj).norm() > 0.0) {
                    w = 1.0 - std::pow((xi - xj).norm() / r, 3);
                }
                sum_w += w;
                C += w * (xj - xiw) * (xj - xiw).transpose();
            }
            // std::cout << C << std::endl;
            C /= sum_w;
        }
        Eigen::JacobiSVD<Eigen::Matrix3d> svd(C, Eigen::ComputeFullU | Eigen::ComputeFullV);
        Eigen::Matrix3d R = svd.matrixU();
        Eigen::Vector3d sigmas = svd.singularValues();
        const auto kr = 4.0;
        const auto ks = 1400.0;
        const auto kn = 0.5;
        const int Ne = 25;
        double sigma1 = sigmas[0];
        for (int i = 0; i < 3; i++) {
            sigmas[i] = std::fmax(sigmas[i], sigma1 / kr);
        }
        auto N = (int)neighbors[i].size();
        // std::cout << "N: " << N << std::endl;
        Eigen::DiagonalMatrix<double, 3> Sigma;
        if (N < Ne) {
            Sigma = kn * Eigen::Vector3d::Ones().asDiagonal();
        } else {
            Sigma = ks * sigmas.asDiagonal();
        }
        G[i] = 1.0 / h * R * Sigma.inverse() * R.transpose();
        // G[i] = 1.0 / h * Eigen::Matrix3d::Identity();
        // std::cout << G[i] << std::endl;
    });
    for (auto &cell : grid) {
        cell.particles.clear();
    }
    for (int i = 0; i < P.rows(); i++) {
        Eigen::Vector3i ip = get_nn_cell_index(P.row(i));
        auto idx = get_nn_linear_index(ip);
        CHECK(idx < grid.size());
        grid[idx].particles.push_back(i);
    }
    neighbors.clear();
    neighbors.resize(GV.rows());
    // for (int i = 0; i < GV.rows(); i++) {
    tbb::parallel_for<int>(0, GV.rows(), [&](int i) {
        // int x = i % res[0];
        // int y = (i % (res[0] * res[1])) / res[0];
        // int z = i / (res[0] * res[1]);
        // Eigen::Vector3i gv_p(x, y, z);
        Eigen::Vector3d p = GV.row(i);
        Eigen::Vector3i ip = get_nn_cell_index(p);
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                for (int dz = -1; dz <= 1; dz++) {
                    Eigen::Vector3i cell_idx = ip + Eigen::Vector3i(dx, dy, dz);
                    if ((cell_idx.array() >= Eigen::Array3i::Zero()).all() &&
                        (cell_idx.array() < nn_grid_size.array()).all()) {
                        auto &cell = grid[get_nn_linear_index(cell_idx)];
                        for (auto &j : cell.particles) {
                            Eigen::Vector3d q = P.row(j);
                            if ((p - q).norm() < 2 * h) {
                                neighbors[i].push_back(j);
                            }
                        }
                    }
                }
            }
        }
    });

    // for (int i = 0; i < GV.rows(); i++) {
    tbb::parallel_for<int>(0, GV.rows(), [&](int i) {
        const double r = 2 * h;
        // Eigen::Vector3i ip;
        // {
        //     int x = i % res[0];
        //     int y = (i % (res[0] * res[1])) / res[0];
        //     int z = i / (res[0] * res[1]);
        //     ip = Eigen::Vector3i(x, y, z);
        // }
        Eigen::Vector3d x = GV.row(i);
        double s = 0.0;
        for (auto j : neighbors[i]) {
            Eigen::Vector3d xj = X.row(j);
            Eigen::Vector3d r = x - xj;
            auto W = W_k / (h * h) * G[j].norm() * kernel_P((G[j] * r).norm());
            // std::cout << (G[j] * r).norm() << std::endl;
            // std::cout << (r.norm() / h) << std::endl;
            s += mass[j] / density[j] * W;
        }
        S[i] = s;

        // double s = 0.0;
        // for (auto j : neighbors[i]) {
        //     Eigen::Vector3d xj = P.row(j);
        //     Eigen::Vector3d r = x - xj;
        //     auto W = W_k / (h * h * h) * kernel_P(r.norm() / h);
        //     s += mass[j] / density[j] * W;
        // }

        // S[i] = s;
    });
    // std::cout << S << std::endl;
    printf("S.mean() = %f\n", S.mean());

    marching_cubes(S, GV, res[0], res[1], res[2], isovalue, V, F);
}