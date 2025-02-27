#include "TRShadingPipeline.h"

#include <algorithm>
#include <iostream>

namespace TinyRenderer
{
	//----------------------------------------------VertexData 23.10.25----------------------------------------------

	
	float TRShadingPipeline::m_ambient_intensity = 1.0f;  // 默认环境光强度为 1.0
	
	TRShadingPipeline::VertexData TRShadingPipeline::VertexData::lerp(
		const TRShadingPipeline::VertexData &v0,
		const TRShadingPipeline::VertexData &v1,
		float frac)
	{
		//Linear interpolation
		VertexData result;
		result.pos = (1.0f - frac) * v0.pos + frac * v1.pos;
		result.col = (1.0f - frac) * v0.col + frac * v1.col;
		result.nor = (1.0f - frac) * v0.nor + frac * v1.nor;
		result.tex = (1.0f - frac) * v0.tex + frac * v1.tex;
		result.cpos = (1.0f - frac) * v0.cpos + frac * v1.cpos;
		result.spos.x = (1.0f - frac) * v0.spos.x + frac * v1.spos.x;
		result.spos.y = (1.0f - frac) * v0.spos.y + frac * v1.spos.y;

		result.TBN = v0.TBN;

		return result;
	}

	TRShadingPipeline::VertexData TRShadingPipeline::VertexData::barycentricLerp(
		const VertexData &v0, 
		const VertexData &v1, 
		const VertexData &v2,
		const glm::vec3 &w)
	{
		VertexData result;
		result.pos = w.x * v0.pos + w.y * v1.pos + w.z * v2.pos;
		result.col = w.x * v0.col + w.y * v1.col + w.z * v2.col;
		result.nor = w.x * v0.nor + w.y * v1.nor + w.z * v2.nor;
		result.tex = w.x * v0.tex + w.y * v1.tex + w.z * v2.tex;
		result.cpos = w.x * v0.cpos + w.y * v1.cpos + w.z * v2.cpos;
		result.spos.x = w.x * v0.spos.x + w.y * v1.spos.x + w.z * v2.spos.x;
		result.spos.y = w.x * v0.spos.y + w.y * v1.spos.y + w.z * v2.spos.y;

		result.TBN = v0.TBN;

		return result;
	}

	void TRShadingPipeline::VertexData::prePerspCorrection(VertexData &v)
	{
		//Perspective correction: the world space properties should be multipy by 1/w before rasterization
		//https://zhuanlan.zhihu.com/p/144331875
		//We use pos.w to store 1/w
		float one_div_w =  1.0f / v.cpos.w;
		v.pos = glm::vec4(v.pos.x * one_div_w, v.pos.y * one_div_w, v.pos.z * one_div_w, one_div_w);
		v.tex = v.tex * one_div_w;
		v.nor = v.nor * one_div_w;
		v.col = v.col * one_div_w;
	}

	void TRShadingPipeline::VertexData::aftPrespCorrection(VertexData &v)
	{
		//Perspective correction: the world space properties should be multipy by w after rasterization
		//https://zhuanlan.zhihu.com/p/144331875
		//We use pos.w to store 1/w
		float w = 1.0f / v.pos.w;
		//v.cpos.z *= w;
		v.pos = glm::vec4(v.pos.x * w, v.pos.y * w, v.pos.z * w, v.pos.w);
		v.tex = v.tex * w;
		v.nor = v.nor * w;
		v.col = v.col * w;
	}

	//----------------------------------------------TRShadingPipeline----------------------------------------------

	std::vector<TRTexture2D::ptr> TRShadingPipeline::m_global_texture_units = {};
	std::vector<TRPointLight> TRShadingPipeline::m_point_lights = {};
	glm::vec3 TRShadingPipeline::m_viewer_pos = glm::vec3(0.0f);


	void TRShadingPipeline::rasterize_wire(
		const VertexData &v0,
		const VertexData &v1,
		const VertexData &v2,
		const unsigned int &screen_width,
		const unsigned int &screene_height,
		std::vector<VertexData> &rasterized_points)
	{
		//Draw each line step by step
		rasterize_wire_aux(v0, v1, screen_width, screene_height, rasterized_points);
		rasterize_wire_aux(v1, v2, screen_width, screene_height, rasterized_points);
		rasterize_wire_aux(v0, v2, screen_width, screene_height, rasterized_points);
	}

	void TRShadingPipeline::rasterize_fill_edge_function(
		const VertexData &v0,
		const VertexData &v1,
		const VertexData &v2,
		const unsigned int &screen_width,
		const unsigned int &screene_height,
		std::vector<VertexData> &rasterized_points)
	{
		VertexData v[] = { v0, v1, v2 };
		//Edge-equations rasterization algorithm
		glm::ivec2 bounding_min;
		glm::ivec2 bounding_max;
		bounding_min.x = std::max(std::min(v0.spos.x, std::min(v1.spos.x, v2.spos.x)), 0);
		bounding_min.y = std::max(std::min(v0.spos.y, std::min(v1.spos.y, v2.spos.y)), 0);
		bounding_max.x = std::min(std::max(v0.spos.x, std::max(v1.spos.x, v2.spos.x)), (int)screen_width - 1);
		bounding_max.y = std::min(std::max(v0.spos.y, std::max(v1.spos.y, v2.spos.y)), (int)screene_height - 1);

		//Adjust the order
		{
			auto e1 = v1.spos - v0.spos;
			auto e2 = v2.spos - v0.spos;
			int orient = e1.x * e2.y - e1.y * e2.x;
			if (orient > 0)
			{
				std::swap(v[1], v[2]);
			}
		}

		//Accelerated Half-Space Triangle Rasterization
		//Refs:Mileff P, Nehéz K, Dudra J. Accelerated half-space triangle rasterization[J].
		//     Acta Polytechnica Hungarica, 2015, 12(7): 217-236. http://acta.uni-obuda.hu/Mileff_Nehez_Dudra_63.pdf

		const glm::ivec2 &A = v[0].spos;
		const glm::ivec2 &B = v[1].spos;
		const glm::ivec2 &C = v[2].spos;

		const int I01 = A.y - B.y, I02 = B.y - C.y, I03 = C.y - A.y;
		const int J01 = B.x - A.x, J02 = C.x - B.x, J03 = A.x - C.x;
		const int K01 = A.x * B.y - A.y * B.x;
		const int K02 = B.x * C.y - B.y * C.x;
		const int K03 = C.x * A.y - C.y * A.x;

		int F01 = I01 * bounding_min.x + J01 * bounding_min.y + K01;
		int F02 = I02 * bounding_min.x + J02 * bounding_min.y + K02;
		int F03 = I03 * bounding_min.x + J03 * bounding_min.y + K03;

		//Degenerated to a line or a point
		if (F01 + F02 + F03 == 0)
			return;

		const float one_div_delta = 1.0f / (F01 + F02 + F03);

		//Top left fill rule
		int E1_t = (((B.y > A.y) || (A.y == B.y && A.x > B.x)) ? 0 : 0);
		int E2_t = (((C.y > B.y) || (B.y == C.y && B.x > C.x)) ? 0 : 0);
		int E3_t = (((A.y > C.y) || (C.y == A.y && C.x > A.x)) ? 0 : 0);

		int Cy1 = F01, Cy2 = F02, Cy3 = F03;
		for (int y = bounding_min.y; y <= bounding_max.y; ++y)
		{
			int Cx1 = Cy1, Cx2 = Cy2, Cx3 = Cy3;
			for (int x = bounding_min.x; x <= bounding_max.x; ++x)
			{
				int E1 = Cx1 + E1_t, E2 = Cx2 + E2_t, E3 = Cx3 + E3_t;
				//Counter-clockwise winding order
				if (E1 <= 0 && E2 <= 0 && E3 <= 0)
				{
					glm::vec3 uvw(Cx2 * one_div_delta, Cx3 * one_div_delta, Cx1 * one_div_delta);
					auto rasterized_point = TRShadingPipeline::VertexData::barycentricLerp(v[0], v[1], v[2], uvw);
					rasterized_point.spos = glm::ivec2(x, y);
					rasterized_points.push_back(rasterized_point);
				}
				Cx1 += I01; Cx2 += I02; Cx3 += I03;
			}
			Cy1 += J01; Cy2 += J02; Cy3 += J03;
		}

	}

	void TRShadingPipeline::rasterize_wire_aux(
		const VertexData &from,
		const VertexData &to,
		const unsigned int &screen_width,
		const unsigned int &screen_height,
		std::vector<VertexData> &rasterized_points)
	{
		//Bresenham line rasterization

		int dx = to.spos.x - from.spos.x;
		int dy = to.spos.y - from.spos.y;
		int stepX = 1, stepY = 1;

		// judge the sign
		if (dx < 0)
		{
			stepX = -1;
			dx = -dx;
		}
		if (dy < 0)
		{
			stepY = -1;
			dy = -dy;
		}

		int d2x = 2 * dx, d2y = 2 * dy;
		int d2y_minus_d2x = d2y - d2x;
		int sx = from.spos.x;
		int sy = from.spos.y;

		// slope < 1.
		if (dy <= dx)
		{
			int flag = d2y - dx;
			for (int i = 0; i <= dx; ++i)
			{
				auto mid = VertexData::lerp(from, to, static_cast<float>(i) / dx);
				mid.spos = glm::ivec2(sx, sy);
				if (mid.spos.x >= 0 && mid.spos.x <= screen_width && mid.spos.y >= 0 && mid.spos.y <= screen_height)
				{
					rasterized_points.push_back(mid);
				}
				sx += stepX;
				if (flag <= 0)
				{
					flag += d2y;
				}
				else
				{
					sy += stepY;
					flag += d2y_minus_d2x;
				}
			}
		}
		// slope > 1.
		else
		{
			int flag = d2x - dy;
			for (int i = 0; i <= dy; ++i)
			{
				auto mid = VertexData::lerp(from, to, static_cast<float>(i) / dy);
				mid.spos = glm::ivec2(sx, sy);
				if (mid.spos.x >= 0 && mid.spos.x < screen_width && mid.spos.y >= 0 && mid.spos.y < screen_height)
				{
					rasterized_points.push_back(mid);
				}
				sy += stepY;
				if (flag <= 0)
				{
					flag += d2x;
				}
				else
				{
					sx += stepX;
					flag -= d2y_minus_d2x;
				}
			}
		}
	}

	int TRShadingPipeline::upload_texture_2D(TRTexture2D::ptr tex)
	{
		if (tex != nullptr)
		{
			m_global_texture_units.push_back(tex);
			return m_global_texture_units.size() - 1;
		}
		return -1;
	}

	TRTexture2D::ptr TRShadingPipeline::getTexture2D(int index)
	{
		if (index < 0 || index >= m_global_texture_units.size())
			return nullptr;
		return m_global_texture_units[index];
	}
	
	int TRShadingPipeline::addPointLight(glm::vec3 pos, glm::vec3 atten, glm::vec3 color)
	{
		m_point_lights.push_back(TRPointLight(pos, atten, color));
		return m_point_lights.size() - 1;
	}

	TRPointLight &TRShadingPipeline::getPointLight(int index)
	{
		return m_point_lights[index];
	}
	
	glm::vec4 TRShadingPipeline::texture2D(const unsigned int &id, const glm::vec2 &uv)
	{
		if (id < 0 || id >= m_global_texture_units.size())
			return glm::vec4(0.0f);
		return m_global_texture_units[id]->sample(uv);
	}


	//----------------------------------------------TRDefaultShadingPipeline----------------------------------------------

	void TRDefaultShadingPipeline::vertexShader(VertexData &vertex)
	{
		//Local space -> World space -> Camera space -> Project space
		vertex.pos = m_model_matrix * glm::vec4(vertex.pos.x, vertex.pos.y, vertex.pos.z, 1.0f);
		vertex.nor = glm::normalize(m_inv_trans_model_matrix * vertex.nor);
		vertex.cpos = m_view_project_matrix * vertex.pos;

		glm::vec3 T = glm::normalize(m_inv_trans_model_matrix * m_tangent);
		glm::vec3 B = glm::normalize(m_inv_trans_model_matrix * m_bitangent);
		vertex.TBN = glm::mat3(T, B, vertex.nor);
	}

	void TRDefaultShadingPipeline::fragmentShader(const VertexData &data, glm::vec4 &fragColor)
	{
		//Just return the color.
		fragColor = glm::vec4(data.tex, 0.0, 1.0f);
	}

	//----------------------------------------------TRTextureShadingPipeline----------------------------------------------

	void TRTextureShadingPipeline::fragmentShader(const VertexData &data, glm::vec4 &fragColor)
	{
		//Default color
		fragColor = glm::vec4(m_ke, 1.0f);

		if (m_diffuse_tex_id != -1)
		{
			fragColor = texture2D(m_diffuse_tex_id, data.tex);
		}
	}

	//----------------------------------------------TRPhongShadingPipeline----------------------------------------------

	void TRPhongShadingPipeline::fragmentShader(const VertexData &data, glm::vec4 &fragColor)
	{
		/*
		fragColor = glm::vec4(0.0f);

		//Fetch the corresponding color 
		glm::vec3 amb_color, dif_color, spe_color, glow_color;
		amb_color = dif_color = (m_diffuse_tex_id != -1) ? glm::vec3(texture2D(m_diffuse_tex_id, data.tex)) : m_kd;
		spe_color = (m_specular_tex_id != -1) ? glm::vec3(texture2D(m_specular_tex_id, data.tex)) : m_ks;
		glow_color = (m_glow_tex_id != -1) ? glm::vec3(texture2D(m_glow_tex_id, data.tex)) : m_ke;

		//No lighting
		if (!m_lighting_enable)
		{
			fragColor = glm::vec4(glow_color, 1.0f);
			return;
		}

		//Calculate the lighting
		glm::vec3 fragPos = glm::vec3(data.pos);
		glm::vec3 normal = glm::normalize(data.nor);
		glm::vec3 viewDir = glm::normalize(m_viewer_pos - fragPos);


		for (size_t i = 0; i < m_point_lights.size(); ++i)
		{
			const auto &light = m_point_lights[i];
			glm::vec3 lightDir = glm::normalize(light.lightPos - fragPos);

			glm::vec3 ambient, diffuse, specular;
			float attenuation = 1.0f;


			// 环境光计算：ambient = ka * Ia
			ambient = amb_color * light.lightColor * m_ambient_intensity;  // 环境光的颜色 = 环境光反射系数 * 光源环境光强度 * 环境光强度

			// 漫反射光计算：diffuse = kd * Id * max(0, dot(L, N))
			float diff = glm::max(glm::dot(normal, lightDir), 0.0f);  // 点积计算法线与光源方向的夹角，确保漫反射光强度为非负
			diffuse = dif_color * light.lightColor * diff;  // 漫反射光的颜色 = 漫反射反射系数 * 光源漫反射强度 * 点积值

			// 镜面反射光计算：specular = ks * Is * max(0, dot(R, V))^shininess
			
			glm::vec3 reflectDir = glm::reflect(-lightDir, normal);  // 计算反射光线的方向
			float spec = glm::pow(glm::max(glm::dot(viewDir, reflectDir), 0.0f), m_shininess);  // 计算镜面反射光的强度，使用视角与反射方向的点积
			specular = spe_color * light.lightColor * spec;  // 镜面反射光的颜色 = 镜面反射系数 * 光源镜面反射强度 * 镜面反射强度
			
			// Blinn-Phong镜面反射光计算
			
		//	glm::vec3 halfDir = glm::normalize(viewDir + lightDir); // 计算半程向量 H = normalize(V + L)
		//	float spec = glm::pow(glm::max(glm::dot(normal, halfDir), 0.0f), m_shininess); // 使用半程向量计算镜面反射强度
		//	specular = spe_color * light.lightColor * spec; // 镜面反射光的颜色 = 镜面反射系数 * 光源镜面反射强度 * 镜面反射强度
			

			// 光照衰减计算：基于距离的衰减
			float distance = glm::length(light.lightPos - fragPos);  // 计算光源到片元的距离
			attenuation = 1.0f / (light.attenuation.x + light.attenuation.y * distance + light.attenuation.z * distance * distance);  // 根据光源距离计算衰减

			// 将衰减应用到最终的光照颜色中
			fragColor.x += (ambient.x + diffuse.x + specular.x) * attenuation;  // 红色通道
			fragColor.y += (ambient.y + diffuse.y + specular.y ) * attenuation;  // 绿色通道
			fragColor.z += (ambient.z + diffuse.z + specular.z) * attenuation;  // 蓝色通道
		}
		*/


		fragColor = glm::vec4(0.0f);

		//Fetch the corresponding color 
		glm::vec3 amb_color, dif_color, spe_color, glow_color;
		amb_color = dif_color = (m_diffuse_tex_id != -1) ? glm::vec3(texture2D(m_diffuse_tex_id, data.tex)) : m_kd;
		spe_color = (m_specular_tex_id != -1) ? glm::vec3(texture2D(m_specular_tex_id, data.tex)) : m_ks;
		glow_color = (m_glow_tex_id != -1) ? glm::vec3(texture2D(m_glow_tex_id, data.tex)) : m_ke;

		std::vector<glm::vec3> vectors;
		vectors.push_back(m_point_lights[0].lightPos);
		vectors.push_back(m_point_lights[1].lightPos);
		vectors.push_back(m_point_lights[2].lightPos);
		glm::vec3 sum(0.0f, 0.0f, 0.0f);
		for (const glm::vec3& vector : vectors) {
			sum += vector;
		}
		int numVectors = vectors.size();
		glm::vec3 average = sum / static_cast<float>(numVectors);
		//No lighting
		if (!m_lighting_enable)
		{
			fragColor = glm::vec4(glow_color, 1.0f);
			return;
		}
		//Calculate the lighting
		glm::vec3 fragPos = glm::vec3(data.pos);  // 片段的世界空间位置
		glm::vec3 normal = glm::normalize(data.nor);  // 片段的法线向量
		glm::vec3 viewDir = glm::normalize(m_viewer_pos - fragPos);  // 视角方向
		for (size_t i = 0; i < m_point_lights.size(); ++i)
		{
			const auto& light = m_point_lights[i];
			glm::vec3 lightDir = glm::normalize(light.lightPos - fragPos);  // 光源到片段的方向
			glm::vec3 ambient, diffuse, specular;
			float attenuation = 1.0f;
			{
				// 计算聚光灯的光强度
				float theta = glm::dot(lightDir, glm::normalize(-(average - light.lightPos)));
				float cutOff = glm::cos(glm::radians(12.5f));
				float outerCutOff = glm::cos(glm::radians(17.5f));
				float epsilon = cutOff - outerCutOff;
				float intensity = glm::clamp((theta - outerCutOff) / epsilon, 0.0f, 1.0f);

				// 环境光、漫反射、镜面反射的计算
				ambient = amb_color * light.lightColor;
				diffuse = dif_color * light.lightColor * glm::max(glm::dot(normal, lightDir), 0.0f);
				glm::vec3 halfwayDir = glm::normalize(viewDir + lightDir);
				specular = spe_color * light.lightColor * glm::pow(glm::max(glm::dot(normal, halfwayDir), 0.0f), m_shininess);

				// 将光强度应用到漫反射和镜面反射
				diffuse *= intensity;
				specular *= intensity;

				// 计算光源的衰减
				float distance = glm::length(light.lightPos - fragPos);
				attenuation = 1.0f / (light.attenuation.x + light.attenuation.y * distance + light.attenuation.z * distance * distance);
			}
			// 累加每个光源的光照结果
			fragColor.x += (ambient.x + diffuse.x + specular.x) * attenuation;
			fragColor.y += (ambient.y + diffuse.y + specular.y) * attenuation;
			fragColor.z += (ambient.z + diffuse.z + specular.z) * attenuation;
		}

		
		// 添加发光（glow）效果
		fragColor = glm::vec4(fragColor.x + glow_color.x, fragColor.y + glow_color.y, fragColor.z + glow_color.z, 1.0f);
		// 色调映射：从高动态范围（HDR）转换为低动态范围（LDR）
		//Tone mapping: HDR -> LDR
		//Refs: https://learnopengl.com/Advanced-Lighting/HDR
		{
			glm::vec3 hdrColor(fragColor);
			fragColor.x = 1.0f - glm::exp(-hdrColor.x * 2.0f);
			fragColor.y = 1.0f - glm::exp(-hdrColor.y * 2.0f);
			fragColor.z = 1.0f - glm::exp(-hdrColor.z * 2.0f);
		}
		



		}

		

	void TRPhongShadingPipeline::fetchFragmentColor(glm::vec3 &amb, glm::vec3 &diff, glm::vec3 &spe, const glm::vec2 &uv) const
	{
		amb = diff = (m_diffuse_tex_id != -1) ? glm::vec3(texture2D(m_diffuse_tex_id, uv)) : m_kd;
		spe = (m_specular_tex_id != -1) ?glm::vec3(texture2D(m_specular_tex_id, uv)) : m_ks;
	}


}