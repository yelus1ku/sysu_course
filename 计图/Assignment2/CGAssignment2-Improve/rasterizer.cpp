#include <algorithm>
#include <vector>
#include "rasterizer.hpp"
#include <opencv2/opencv.hpp>
#include <math.h>


rst::pos_buf_id rst::rasterizer::load_positions(const std::vector<Eigen::Vector3f>& positions)
{
    auto id = get_next_id();
    pos_buf.emplace(id, positions);

    return { id };
}

rst::ind_buf_id rst::rasterizer::load_indices(const std::vector<Eigen::Vector3i>& indices)
{
    auto id = get_next_id();
    ind_buf.emplace(id, indices);

    return { id };
}

rst::col_buf_id rst::rasterizer::load_colors(const std::vector<Eigen::Vector3f>& cols)
{
    auto id = get_next_id();
    col_buf.emplace(id, cols);

    return { id };
}

auto to_vec4(const Eigen::Vector3f& v3, float w = 1.0f)
{
    return Vector4f(v3.x(), v3.y(), v3.z(), w);
}

static float cross2D(float x1, float y1, float x2, float y2)
{
    return x1 * y2 - x2 * y1;
}

static bool insideTriangle(float x, float y, const Vector3f* _v)
{
    // TODO 2: Implement this function to check if the point (x, y) is inside the triangle represented by _v[0], _v[1], _v[2]
    Eigen::Vector3f p(x, y, 0.0f);
    Eigen::Vector3f v01 = _v[1] - _v[0];
    Eigen::Vector3f c01 = v01.cross(p - _v[0]);
    Eigen::Vector3f v12 = _v[2] - _v[1];
    Eigen::Vector3f c12 = v12.cross(p - _v[1]);
    Eigen::Vector3f v20 = _v[0] - _v[2];
    Eigen::Vector3f c20 = v20.cross(p - _v[2]);
    // 如果方向相同，返回真
    if ((c01[2] > 0 && c12[2] > 0 && c20[2] > 0) || (c01[2] < 0 && c12[2] < 0 && c20[2] < 0))
        return true;
    else
        return false;
}

static std::tuple<float, float, float> computeBarycentric2D(float x, float y, const Vector3f* v)
{
    float c1 = (x * (v[1].y() - v[2].y()) + (v[2].x() - v[1].x()) * y + v[1].x() * v[2].y() - v[2].x() * v[1].y()) / (v[0].x() * (v[1].y() - v[2].y()) + (v[2].x() - v[1].x()) * v[0].y() + v[1].x() * v[2].y() - v[2].x() * v[1].y());
    float c2 = (x * (v[2].y() - v[0].y()) + (v[0].x() - v[2].x()) * y + v[2].x() * v[0].y() - v[0].x() * v[2].y()) / (v[1].x() * (v[2].y() - v[0].y()) + (v[0].x() - v[2].x()) * v[1].y() + v[2].x() * v[0].y() - v[0].x() * v[2].y());
    float c3 = (x * (v[0].y() - v[1].y()) + (v[1].x() - v[0].x()) * y + v[0].x() * v[1].y() - v[1].x() * v[0].y()) / (v[2].x() * (v[0].y() - v[1].y()) + (v[1].x() - v[0].x()) * v[2].y() + v[0].x() * v[1].y() - v[1].x() * v[0].y());
    return { c1,c2,c3 };
}

void rst::rasterizer::draw(pos_buf_id pos_buffer, ind_buf_id ind_buffer, col_buf_id col_buffer, Primitive type)
{
    auto& buf = pos_buf[pos_buffer.pos_id];
    auto& ind = ind_buf[ind_buffer.ind_id];
    auto& col = col_buf[col_buffer.col_id];

    float f1 = (50 - 0.1) / 2.0;
    float f2 = (50 + 0.1) / 2.0;

    Eigen::Matrix4f mvp = projection * view * model;
    for (auto& i : ind)
    {
        Triangle t;
        Eigen::Vector4f v[] = {
                mvp * to_vec4(buf[i[0]], 1.0f),
                mvp * to_vec4(buf[i[1]], 1.0f),
                mvp * to_vec4(buf[i[2]], 1.0f)
        };
        //Homogeneous division
        for (auto& vec : v) {
            vec /= vec.w();
        }
        //Viewport transformation
        for (auto& vert : v)
        {
            vert.x() = 0.5 * width * (vert.x() + 1.0);
            vert.y() = 0.5 * height * (vert.y() + 1.0);
            vert.z() = vert.z() * f1 + f2;
        }

        for (int i = 0; i < 3; ++i)
        {
            t.setVertex(i, v[i].head<3>());
            t.setVertex(i, v[i].head<3>());
            t.setVertex(i, v[i].head<3>());
        }

        auto col_x = col[i[0]];
        auto col_y = col[i[1]];
        auto col_z = col[i[2]];

        t.setColor(0, col_x[0], col_x[1], col_x[2]);
        t.setColor(1, col_y[0], col_y[1], col_y[2]);
        t.setColor(2, col_z[0], col_z[1], col_z[2]);

        rasterize_triangle(t);
    }
}

//Screen space rasterization
void rst::rasterizer::rasterize_triangle(const Triangle& t) {
   
    //  提升任务---------------------------- -
   // 将 toVector4 的结果赋值给 vertices
    auto vertices = t.toVector4();

    // 找到三角形的包围盒
    int minX = std::min({ static_cast<int>(vertices.at(0).x()), static_cast<int>(vertices.at(1).x()), static_cast<int>(vertices.at(2).x()) });
    int minY = std::min({ static_cast<int>(vertices.at(0).y()), static_cast<int>(vertices.at(1).y()), static_cast<int>(vertices.at(2).y()) });
    int maxX = std::max({ static_cast<int>(vertices.at(0).x()), static_cast<int>(vertices.at(1).x()), static_cast<int>(vertices.at(2).x()) });
    int maxY = std::max({ static_cast<int>(vertices.at(0).y()), static_cast<int>(vertices.at(1).y()), static_cast<int>(vertices.at(2).y()) });

    //遍历包围盒内的像素
    for (int x = minX; x <= maxX; x++) {
        for (int y = minY; y <= maxY; y++) {
            if (insideTriangle(x, y, t.v)) {
                ///计算重心坐标
                auto [alpha, beta, gamma] = computeBarycentric2D(x, y, t.v);
                //计算插值后的深度值
                float w_reciprocal = 1.0f / (alpha / vertices.at(0).w() + beta / vertices.at(1).w() + gamma / vertices.at(2).w());
                float z_interpolated = alpha * vertices.at(0).z() / vertices.at(0).w() + beta * vertices.at(1).z() / vertices.at(1).w() + gamma * vertices.at(2).z() / vertices.at(2).w();
                z_interpolated *= w_reciprocal;
               //深度测试和颜色更新
                int index = get_index(x, y);

                if (z_interpolated < depth_buf[index]) {
                    set_pixel(Eigen::Vector3f(x, y, z_interpolated), t.getColor());
                    depth_buf[index] = z_interpolated;
                }
            }
        }
    }
    
    /*
    //挑战任务-------------------------------------------------------------------------------------------------------------------------------
    auto vertices = t.toVector4();
    
    float xmin = std::min(vertices[0][0],MIN(vertices[1][0], vertices[2][0]));
    float ymin = std::min(vertices[0][1], MIN(vertices[1][1], vertices[2][1]));
    float xmax =  std::max(vertices[0][0],MAX(vertices[1][0], vertices[2][0]));
    float ymax = std::max(vertices[0][1], MAX(vertices[1][1], vertices[2][1]));
    
    // 遍历包围盒内的所有像素
    for (int x = static_cast<int>(xmin); x <= xmax; ++x) 
    {
        for (int y = static_cast<int>(ymin); y <= ymax; ++y) 
        {
            for (int subx = 0; subx < 2; subx++) // 在一个像素内进行 2x2 超采样，遍历子像素
            {
                for (int suby = 0; suby < 2; suby++)
                {
                   
                    float x1 = x + 0.5 * subx; // 子像素的x坐标，偏移0.5实现超采样
                    float y1 = y + 0.5 * suby; // 子像素的y坐标，偏移0.5实现超采样

                    // 判断子采样点是否在当前三角形内
                    if (!insideTriangle(x1, y1, t.v))
                        continue; // 如果不在三角形内，跳过该点的处理

                    // 计算该点的重心坐标
                    auto [alpha, beta, gamma] = computeBarycentric2D(x1, y1, t.v);

                    // 计算逆深度加权因子，用于插值矫正
                    float w_reciprocal = 1.0 / (alpha / vertices[0].w() + beta / vertices[1].w() + gamma / vertices[2].w());

                    // 计算插值后的深度值（z坐标），并应用归一化
                    float z_interpolated = alpha * vertices[0].z() / vertices[0].w() +
                        beta * vertices[1].z() / vertices[1].w() +
                        gamma * vertices[2].z() / vertices[2].w();
                    z_interpolated *= w_reciprocal;

                    // 深度测试：检查当前深度值是否小于深度缓冲中的值
                    if (z_interpolated < depth_buf[get_subindex(x, y, subx, suby)]) {
                        depth_buf[get_subindex(x, y, subx, suby)] = z_interpolated; // 更新深度缓冲
                        preframe_buf[get_subindex(x, y, subx, suby)] = t.getColor(); // 更新颜色缓冲
                    }
                    
                    
                }
            }

            // 初始化像素点的颜色
            Eigen::Vector3f newcolor(0.0f, 0.0f, 0.0f);

            // 合并子采样点的颜色值，计算像素点的最终颜色
            for (int subx = 0; subx < 2; subx++)
            {
                for (int suby = 0; suby < 2; suby++)
                    newcolor += preframe_buf[get_subindex(x, y, subx, suby)]; // 累加子采样点颜色
            }

            // 将像素的最终颜色设置为子采样点颜色的平均值
            set_pixel(Vector3f(x, y, 1.0f), newcolor / 4); // 平均 4 个子像素的颜色
        }
    }

    */
}

void rst::rasterizer::set_model(const Eigen::Matrix4f& m)
{
    model = m;
}

void rst::rasterizer::set_view(const Eigen::Matrix4f& v)
{
    view = v;
}

void rst::rasterizer::set_projection(const Eigen::Matrix4f& p)
{
    projection = p;
}

void rst::rasterizer::clear(rst::Buffers buff)
{
    if ((buff & rst::Buffers::Color) == rst::Buffers::Color)
    {
        std::fill(preframe_buf.begin(), preframe_buf.end(), Eigen::Vector3f{ 0, 0, 0 });
        std::fill(frame_buf.begin(), frame_buf.end(), Eigen::Vector3f{ 0, 0, 0 });
    }
    if ((buff & rst::Buffers::Depth) == rst::Buffers::Depth)
    {
        std::fill(depth_buf.begin(), depth_buf.end(), std::numeric_limits<float>::infinity());
    }
}

rst::rasterizer::rasterizer(int w, int h) : width(w), height(h)
{
    preframe_buf.resize(4 * w * h);
    frame_buf.resize(w * h);
    depth_buf.resize(4 * w * h);
}

int rst::rasterizer::get_index(int x, int y)
{
    return (height - 1 - y) * width + x;
}

int rst::rasterizer::get_subindex(int x, int y, int subx, int suby)
{
    int X = 2 * x + subx;
    int Y = 2 * y + suby;
    return (2 * height - 1 - Y) * 2 * width + X;
}

void rst::rasterizer::set_pixel(const Eigen::Vector3f& point, const Eigen::Vector3f& color)
{
    //old index: auto ind = point.y() + point.x() * width;
    auto ind = (height - 1 - point.y()) * width + point.x();
    frame_buf[ind] = color;

}

// clang-format on