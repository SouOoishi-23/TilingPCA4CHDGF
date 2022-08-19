#include "HDGF.hpp"
#include "patchPCA.hpp"
#include <opencp.hpp>
using namespace std;
using namespace cv;
enum class BFLSPSchedule
{
	Compute,
	LUT,
	LUTSQRT,
};

void bilateralFilterLocalStatisticsPriorInternal(const std::vector<cv::Mat>& src, std::vector<cv::Mat>& split_inter, const cv::Mat& vecW, const float sigma_range, const float sigma_space, const float delta, cv::Mat& mask, BFLSPSchedule schedule, float* lut = nullptr)
{
	CV_Assert(src.size() == 3);
	CV_Assert(src[0].depth() == CV_32F);

	const float* s0 = src[0].ptr<float>();
	const float* s1 = src[1].ptr<float>();
	const float* s2 = src[2].ptr<float>();
	float* i0 = split_inter[0].ptr<float>();
	float* i1 = split_inter[1].ptr<float>();
	float* i2 = split_inter[2].ptr<float>();
	const float* vecw_ptr = vecW.ptr<float>();
	const float* mask_ptr = mask.ptr<float>();

	const float sqrt2_sr_divpi = float((sqrt(2.0) * sigma_range) / sqrt(CV_PI));
	const float sqrt2_sr_inv = float(1.0 / (sqrt(2.0) * sigma_range));
	const float eps2 = delta * sqrt2_sr_inv;
	const float exp2 = exp(-eps2 * eps2);
	const float erf2 = erf(eps2);
	const int simdsize = src[0].size().area() / 8;
	if (schedule == BFLSPSchedule::Compute)
	{
		const __m256 mexp2 = _mm256_set1_ps(exp2);
		const __m256 merf2 = _mm256_set1_ps(erf2);
		const __m256 mflt_epsilon = _mm256_set1_ps(+FLT_EPSILON);
		const __m256 msqrt2_sr_inv = _mm256_set1_ps(sqrt2_sr_inv);
		const __m256 msqrt2_sr_divpi = _mm256_set1_ps(sqrt2_sr_divpi);
		const __m256 mdelta = _mm256_set1_ps(delta);
		const __m256 mm2f = _mm256_set1_ps(2.f);
		const __m256 mm1f = _mm256_set1_ps(-1.f);
		for (int i = 0; i < simdsize; i++)
		{
			const __m256 ms0 = _mm256_load_ps(s0);
			const __m256 ms1 = _mm256_load_ps(s1);
			const __m256 ms2 = _mm256_load_ps(s2);
			const __m256 mi0 = _mm256_load_ps(i0);
			const __m256 mi1 = _mm256_load_ps(i1);
			const __m256 mi2 = _mm256_load_ps(i2);
			const __m256 mvecw = _mm256_load_ps(vecw_ptr);
			const __m256 norm = _mm256_div_avoidzerodiv_ps(_mm256_set1_ps(1.f), mvecw);
			const __m256 mdiffb = _mm256_sub_ps(ms0, _mm256_mul_ps(norm, mi0));
			const __m256 mdiffg = _mm256_sub_ps(ms1, _mm256_mul_ps(norm, mi1));
			const __m256 mdiffr = _mm256_sub_ps(ms2, _mm256_mul_ps(norm, mi2));
			const __m256 mdiff = _mm256_add_ps(_mm256_sqrt_ps(_mm256_fmadd_ps(mdiffr, mdiffr, _mm256_fmadd_ps(mdiffg, mdiffg, _mm256_mul_ps(mdiffb, mdiffb)))), mflt_epsilon);
			const __m256 meps1 = _mm256_mul_ps(_mm256_fmadd_ps(mm2f, mdiff, mdelta), msqrt2_sr_inv);
			const __m256 mcoeff = _mm256_div_ps(_mm256_sub_ps(_mm256_exp_ps(_mm256_mul_ps(mm1f, _mm256_mul_ps(meps1, meps1))), mexp2), _mm256_add_ps(_mm256_erf_ps(meps1), merf2));
			//const float coeff = (exp(-eps1 * eps1) - exp2) / (erf(eps1) + erf2);
			const __m256 ma = _mm256_div_ps(_mm256_mul_ps(mcoeff, msqrt2_sr_divpi), mdiff);
			const __m256 mmask = _mm256_load_ps(mask_ptr);
			_mm256_store_ps(i0, _mm256_blendv_ps(mi0, _mm256_mul_ps(mvecw, _mm256_fmadd_ps(ma, mdiffb, ms0)), mmask));
			_mm256_store_ps(i1, _mm256_blendv_ps(mi1, _mm256_mul_ps(mvecw, _mm256_fmadd_ps(ma, mdiffg, ms1)), mmask));
			_mm256_store_ps(i2, _mm256_blendv_ps(mi2, _mm256_mul_ps(mvecw, _mm256_fmadd_ps(ma, mdiffr, ms2)), mmask));

			s0 += 8; s1 += 8; s2 += 8;
			i0 += 8; i1 += 8; i2 += 8;
			vecw_ptr += 8;
			mask_ptr += 8;
		}
#if 0
		for (int i = 0; i < src[0].size().area(); i++)
		{
			if (mask_ptr[i] == 0)continue;

			const float norm = 1.f / (vecw_ptr[i] + FLT_EPSILON);
			const float diffb = (s0[i] - norm * i0[i]);
			const float diffg = (s1[i] - norm * i1[i]);
			const float diffr = (s2[i] - norm * i2[i]);
			const float diff = sqrt(diffb * diffb + diffg * diffg + diffr * diffr) + FLT_EPSILON;

			const float eps1 = (2.f * diff + delta) * sqrt2_sr_inv;
			const float coeff = ((exp(-eps1 * eps1) - exp2) / (erf(eps1) + erf2)) * sqrt2_sr_divpi / (diff + FLT_EPSILON);
			i0[i] = vecw_ptr[i] * (s0[i] + coeff * diffb);
			i1[i] = vecw_ptr[i] * (s1[i] + coeff * diffg);
			i2[i] = vecw_ptr[i] * (s2[i] + coeff * diffr);
		}
#endif
	}
	else
	{
		const __m256 mexp2 = _mm256_set1_ps(exp2);
		const __m256 merf2 = _mm256_set1_ps(erf2);
		const __m256 mflt_epsilon = _mm256_set1_ps(+FLT_EPSILON);
		const __m256 msqrt2_sr_inv = _mm256_set1_ps(sqrt2_sr_inv);
		const __m256 msqrt2_sr_divpi = _mm256_set1_ps(sqrt2_sr_divpi);
		const __m256 mdelta = _mm256_set1_ps(delta);
		const __m256 mm2f = _mm256_set1_ps(2.f);
		const __m256 mm1f = _mm256_set1_ps(-1.f);

		for (int i = 0; i < simdsize; i++)
		{
			const __m256 ms0 = _mm256_load_ps(s0);
			const __m256 ms1 = _mm256_load_ps(s1);
			const __m256 ms2 = _mm256_load_ps(s2);
			const __m256 mi0 = _mm256_load_ps(i0);
			const __m256 mi1 = _mm256_load_ps(i1);
			const __m256 mi2 = _mm256_load_ps(i2);
			const __m256 mvecw = _mm256_load_ps(vecw_ptr);
			const __m256 norm = _mm256_div_avoidzerodiv_ps(_mm256_set1_ps(1.f), mvecw);
			const __m256 mdiffb = _mm256_sub_ps(ms0, _mm256_mul_ps(norm, mi0));
			const __m256 mdiffg = _mm256_sub_ps(ms1, _mm256_mul_ps(norm, mi1));
			const __m256 mdiffr = _mm256_sub_ps(ms2, _mm256_mul_ps(norm, mi2));
			const __m256 mdiff = _mm256_add_ps(_mm256_sqrt_ps(_mm256_fmadd_ps(mdiffr, mdiffr, _mm256_fmadd_ps(mdiffg, mdiffg, _mm256_mul_ps(mdiffb, mdiffb)))), mflt_epsilon);

			const __m256 mcoeff = _mm256_i32gather_ps(lut, _mm256_cvtps_epi32(_mm256_min_ps(_mm256_mul_ps(_mm256_set1_ps(10.f), mdiff), _mm256_set1_ps(4430))), 4);

			const __m256 mmask = _mm256_load_ps(mask_ptr);
			_mm256_store_ps(i0, _mm256_blendv_ps(mi0, _mm256_mul_ps(mvecw, _mm256_fmadd_ps(mcoeff, mdiffb, ms0)), mmask));
			_mm256_store_ps(i1, _mm256_blendv_ps(mi1, _mm256_mul_ps(mvecw, _mm256_fmadd_ps(mcoeff, mdiffg, ms1)), mmask));
			_mm256_store_ps(i2, _mm256_blendv_ps(mi2, _mm256_mul_ps(mvecw, _mm256_fmadd_ps(mcoeff, mdiffr, ms2)), mmask));

			s0 += 8; s1 += 8; s2 += 8;
			i0 += 8; i1 += 8; i2 += 8;
			vecw_ptr += 8;
			mask_ptr += 8;
		}
	}
}


void ConstantTimeHDGF_InterpolationSingle::alloc(cv::Mat& dst)
{
	if (isUseLocalStatisticsPrior)
	{
		blendLSPMask.create(img_size, CV_32F);
		lsp.resize(3);
		lsp[0].create(img_size, CV_32F);
		lsp[1].create(img_size, CV_32F);
		lsp[2].create(img_size, CV_32F);
	}

	if (downSampleImage != 1)
	{
		vsrcRes.resize(channels);
		vguideRes.resize(guide_channels);

		NumerDenomRes.resize(channels + 1);
		for (int c = 0; c < channels + 1; c++)
		{
			NumerDenomRes[c].create(img_size, CV_32FC1);
		}
	}

	if (alpha.size() != K || alpha[0].size() != img_size)
	{
		alpha.resize(K);
		for (int i = 0; i < K; i++)
		{
			alpha[i].create(img_size, CV_32FC1);
		}
	}

	if (denom.size() != img_size)
		denom.create(img_size, CV_32FC1);

	if (numer.size() != channels || numer[0].size() != img_size)
	{
		numer.resize(channels);
		for (int c = 0; c < channels; c++)
		{
			numer[c].create(img_size, CV_32FC1);
		}
	}

	if (split_inter.size() != channels) split_inter.resize(channels);
	for (int c = 0; c < channels; c++)
	{
		if (split_inter[c].size() != img_size / downSampleImage)
			split_inter[c].create(img_size / downSampleImage, CV_32FC1);
	}
	if (vecW.size() != K || vecW[0].size() != img_size / downSampleImage)
	{
		vecW.resize(K);
		for (int i = 0; i < K; i++)
		{
			vecW[i].create(img_size / downSampleImage, CV_32FC1);
		}
	}

	dst.create(img_size, CV_MAKETYPE(CV_32F, channels));
}

//isUseLocalMu=false, without local mu
template<int use_fmath>
void ConstantTimeHDGF_InterpolationSingle::computeWandAlpha(const std::vector<cv::Mat>& guide)
{
	const float coeff = float(-1.0 / (2.0 * sigma_range * sigma_range));
	const __m256 mcoef = _mm256_set1_ps(coeff);
	const __m256 mlambda = _mm256_set1_ps(-lambda);//for soft assign
	AutoBuffer<float*> w_ptr(K);
	for (int k = 0; k < K; k++)
	{
		w_ptr[k] = vecW[k].ptr<float>();
	}

	const int method = 0;//nk-loop-hard
	//const int method = 1;//nk-loop-soft
	if (method == 0)
	{
		const __m256 margclip = _mm256_set1_ps(float(sigma_range * 6.0 * sigma_range * 6.0));
		const __m256 mone = _mm256_set1_ps(1.f);
		__m256* mk = (__m256*)_mm_malloc(sizeof(__m256) * K, AVX_ALIGN);
		for (int k = 0; k < K; k++)
		{
			mk[k] = _mm256_set1_ps(float(k));
		}

		if (guide_channels == 3)
		{
			const float* im0 = guide[0].ptr<float>();
			const float* im1 = guide[1].ptr<float>();
			const float* im2 = guide[2].ptr<float>();
			cv::AutoBuffer<float*> aptr(K);
			__m256* mc0 = (__m256*)_mm_malloc(sizeof(__m256) * K, AVX_ALIGN);
			__m256* mc1 = (__m256*)_mm_malloc(sizeof(__m256) * K, AVX_ALIGN);
			__m256* mc2 = (__m256*)_mm_malloc(sizeof(__m256) * K, AVX_ALIGN);
			for (int k = 0; k < K; k++)
			{
				aptr[k] = alpha[k].ptr<float>();
				mc0[k] = _mm256_set1_ps(mu.at<cv::Vec3f>(k)[0]);
				mc1[k] = _mm256_set1_ps(mu.at<cv::Vec3f>(k)[1]);
				mc2[k] = _mm256_set1_ps(mu.at<cv::Vec3f>(k)[2]);
			}

			for (int n = 0; n < img_size.area(); n += 8)
			{
				const __m256 mimage0 = _mm256_load_ps(im0 + n);
				const __m256 mimage1 = _mm256_load_ps(im1 + n);
				const __m256 mimage2 = _mm256_load_ps(im2 + n);
				__m256 mdiffmax = _mm256_set1_ps(FLT_MAX);
				__m256 argment = _mm256_set1_ps(0.f);

				for (int k = 0; k < K; k++)
				{
					__m256 msub = _mm256_sub_ps(mimage0, mc0[k]);
					__m256 mdiff = _mm256_mul_ps(msub, msub);
					msub = _mm256_sub_ps(mimage1, mc1[k]);
					mdiff = _mm256_fmadd_ps(msub, msub, mdiff);
					msub = _mm256_sub_ps(mimage2, mc2[k]);
					mdiff = _mm256_fmadd_ps(msub, msub, mdiff);

					mdiff = _mm256_min_ps(margclip, mdiff);

					_mm256_store_ps(aptr[k], v_exp_ps<use_fmath>(_mm256_mul_ps(mcoef, mdiff)));
					aptr[k] += 8;
					_mm256_argmin_ps(mdiff, mdiffmax, argment, float(k));
				}
				for (int k = 0; k < K; k++)
				{
					//_mm256_store_ps(w_ptr[k] + n, _mm256_blendv_ps(_mm256_set1_ps(FLT_MIN), _mm256_set1_ps(1.f), _mm256_cmp_ps(argment, _mm256_set1_ps(k), 0)));
					const __m256 m = _mm256_cmp_ps(argment, mk[k], 0);
					_mm256_store_ps(w_ptr[k] + n, _mm256_blendv_ps(m, mone, m));
				}
			}

			_mm_free(mc0);
			_mm_free(mc1);
			_mm_free(mc2);
		}
		else //n-dimensional signal
		{
			cv::AutoBuffer<float*> gptr(guide_channels);
			cv::AutoBuffer<__m256> mguide(guide_channels);
			for (int c = 0; c < guide_channels; c++) gptr[c] = (float*)guide[c].ptr<float>();

			for (int n = 0; n < img_size.area(); n += 8)
			{
				for (int c = 0; c < guide_channels; c++) mguide[c] = _mm256_load_ps(gptr[c] + n);

				__m256 mdiffmax = _mm256_set1_ps(FLT_MAX);
				__m256 argment = _mm256_setzero_ps();

				for (int k = 0; k < K; k++)
				{
					const float* muPtr = mu.ptr<float>(k);
					__m256 mdiff = _mm256_setzero_ps();
					for (int c = 0; c < guide_channels; c++)
					{
						__m256 msub = _mm256_sub_ps(mguide[c], _mm256_set1_ps(muPtr[c]));
						mdiff = _mm256_fmadd_ps(msub, msub, mdiff);
					}
					mdiff = _mm256_min_ps(margclip, mdiff);

					float* a_ptr = alpha[k].ptr<float>();
					_mm256_store_ps(a_ptr + n, v_exp_ps<use_fmath>(_mm256_mul_ps(mcoef, mdiff)));

					_mm256_argmin_ps(mdiff, mdiffmax, argment, float(k));
				}
				for (int k = 0; k < K; k++)
				{
					//_mm256_store_ps(w_ptr[k] + n, _mm256_blendv_ps(_mm256_set1_ps(FLT_MIN), _mm256_set1_ps(1.f), _mm256_cmp_ps(argment, _mm256_set1_ps(k), 0)));
					_mm256_store_ps(w_ptr[k] + n, _mm256_blendv_ps(_mm256_setzero_ps(), _mm256_set1_ps(1.f), _mm256_cmp_ps(argment, _mm256_set1_ps(float(k)), 0)));
				}
			}
		}

		_mm_free(mk);
	}
	else if (method == 1)
	{
		__m256 margclip = _mm256_set1_ps(float(sigma_range * 6.0 * sigma_range * 6.0));
		float* im0 = vsrc[0].ptr<float>();
		float* im1 = vsrc[1].ptr<float>();
		float* im2 = vsrc[2].ptr<float>();
		for (int n = 0; n < img_size.area(); n += 8)
		{
			const __m256 mimage0 = _mm256_load_ps(im0 + n);
			const __m256 mimage1 = _mm256_load_ps(im1 + n);
			const __m256 mimage2 = _mm256_load_ps(im2 + n);
			__m256 malpha_sum = _mm256_setzero_ps();
			cv::AutoBuffer<__m256> mvalpha(K);
			for (int k = 0; k < K; k++)
			{
				const __m256 mcenter0 = _mm256_set1_ps(mu.at<cv::Vec3f>(k)[0]);
				const __m256 mcenter1 = _mm256_set1_ps(mu.at<cv::Vec3f>(k)[1]);
				const __m256 mcenter2 = _mm256_set1_ps(mu.at<cv::Vec3f>(k)[2]);

				__m256 msub = _mm256_sub_ps(mimage0, mcenter0);
				__m256 mdiff = _mm256_mul_ps(msub, msub);
				msub = _mm256_sub_ps(mimage1, mcenter1);
				mdiff = _mm256_fmadd_ps(msub, msub, mdiff);
				msub = _mm256_sub_ps(mimage2, mcenter2);
				mdiff = _mm256_fmadd_ps(msub, msub, mdiff);

				mdiff = _mm256_min_ps(margclip, mdiff);

				float* a_ptr = alpha[k].ptr<float>();
				_mm256_store_ps(a_ptr + n, v_exp_ps<use_fmath>(_mm256_mul_ps(mcoef, mdiff)));

				const __m256 malpha = v_exp_ps<use_fmath>(_mm256_mul_ps(mlambda, _mm256_sqrt_ps(mdiff)));//Laplacian
				//const __m256 malpha = v_exp_ps<use_fmath>(_mm256_mul_ps(mlambda, mdiff));//Gaussian
				mvalpha[k] = malpha;
				malpha_sum = _mm256_add_ps(malpha_sum, malpha);
			}
			for (int k = 0; k < K; k++)
			{
				float* w_ptr = vecW[k].ptr<float>();
				//_mm256_store_ps(w_ptr + n, _mm256_blendv_ps(_mm256_set1_ps(FLT_MIN), _mm256_set1_ps(1.f), _mm256_cmp_ps(argment, _mm256_set1_ps(k), 0)));
				_mm256_store_ps(w_ptr + n, _mm256_div_ps(mvalpha[k], malpha_sum));
			}
		}
	}
}

//isUseLocalMu=false, without local mu
void ConstantTimeHDGF_InterpolationSingle::computeIndex(const std::vector<cv::Mat>& guide, const std::vector<cv::Mat>& guideRes)
{
	index.create(guide[0].size(), CV_32F);

	const float coeff = float(-1.0 / (2.0 * sigma_range * sigma_range));
	const __m256 mcoef = _mm256_set1_ps(coeff);
	const Size size = img_size / downSampleImage;
	const int imsize = size.area();
	const int IMSIZE8 = imsize / 8;

	if (guide_channels == 1)
	{
		__m256* gptr = (downSampleImage == 1) ? (__m256*)guide[0].ptr<float>() : (__m256*)guideRes[0].ptr<float>();
		__m256* idx = (__m256*)index.ptr<float>();

		AutoBuffer<__m256> mmu(K);
		for (int k = 0; k < K; k++)
		{
			const float* muPtr = mu.ptr<float>(k);
			mmu[k] = _mm256_set1_ps(muPtr[0]);
		}
		for (int n = 0; n < IMSIZE8; n++)
		{
			__m256 mdiffmax = _mm256_set1_ps(FLT_MAX);
			__m256 argment = _mm256_setzero_ps();

			for (int k = 0; k < K; k++)
			{
				const __m256 msub = _mm256_sub_ps(*gptr, mmu[k]);
				const __m256 mdiff = _mm256_mul_ps(msub, msub);

				_mm256_argmin_ps(mdiff, mdiffmax, argment, float(k));
			}
			*idx++ = argment;
			gptr++;
		}
	}
	else if (guide_channels == 3)
	{
		const __m256* im0 = (downSampleImage == 1) ? (__m256*)guide[0].ptr<float>() : (__m256*)guideRes[0].ptr<float>();
		const __m256* im1 = (downSampleImage == 1) ? (__m256*)guide[1].ptr<float>() : (__m256*)guideRes[1].ptr<float>();
		const __m256* im2 = (downSampleImage == 1) ? (__m256*)guide[2].ptr<float>() : (__m256*)guideRes[2].ptr<float>();
		__m256* idx = (__m256*)index.ptr<float>();

		AutoBuffer<__m256> mc0(K);
		AutoBuffer<__m256> mc1(K);
		AutoBuffer<__m256> mc2(K);
		for (int k = 0; k < K; k++)
		{
			mc0[k] = _mm256_set1_ps(mu.at<cv::Vec3f>(k)[0]);
			mc1[k] = _mm256_set1_ps(mu.at<cv::Vec3f>(k)[1]);
			mc2[k] = _mm256_set1_ps(mu.at<cv::Vec3f>(k)[2]);
		}

		for (int n = 0; n < IMSIZE8; n++)
		{
			__m256 mdiffmax = _mm256_set1_ps(FLT_MAX);
			__m256 argment = _mm256_setzero_ps();
			for (int k = 0; k < K; k++)
			{
				__m256 msub = _mm256_sub_ps(*im0, mc0[k]);
				__m256 mdiff = _mm256_mul_ps(msub, msub);
				msub = _mm256_sub_ps(*im1, mc1[k]);
				mdiff = _mm256_fmadd_ps(msub, msub, mdiff);
				msub = _mm256_sub_ps(*im2, mc2[k]);
				mdiff = _mm256_fmadd_ps(msub, msub, mdiff);

				_mm256_argmin_ps(mdiff, mdiffmax, argment, float(k));
			}
			*idx++ = argment;
			im0++; im1++; im2++;
		}
	}
	else //n-dimensional signal
	{
		cv::AutoBuffer<__m256*> gptr(guide_channels);
		for (int c = 0; c < guide_channels; c++) gptr[c] = (downSampleImage == 1) ? (__m256*)guide[c].ptr<float>() : (__m256*)guideRes[c].ptr<float>();
		__m256* idx = (__m256*)index.ptr<float>();

		AutoBuffer<__m256*> mmu(K);
		for (int k = 0; k < K; k++)
		{
			mmu[k] = (__m256*)_mm_malloc(guide_channels * sizeof(__m256), AVX_ALIGN);
			const float* muPtr = mu.ptr<float>(k);
			for (int c = 0; c < guide_channels; c++)
			{
				mmu[k][c] = _mm256_set1_ps(muPtr[c]);
			}
		}

		for (int n = 0; n < IMSIZE8; n++)
		{
			__m256 mdiffmax = _mm256_set1_ps(FLT_MAX);
			__m256 argment = _mm256_setzero_ps();

			for (int k = 0; k < K; k++)
			{
				__m256 mdiff;
				{
					const __m256 msub = _mm256_sub_ps(*gptr[0], mmu[k][0]);
					mdiff = _mm256_mul_ps(msub, msub);
				}
				for (int c = 1; c < guide_channels; c++)
				{
					const __m256 msub = _mm256_sub_ps(*gptr[c], mmu[k][c]);
					mdiff = _mm256_fmadd_ps(msub, msub, mdiff);
				}

				_mm256_argmin_ps(mdiff, mdiffmax, argment, float(k));
			}
			*idx++ = argment;
			for (int c = 0; c < guide_channels; c++)gptr[c]++;
		}

		for (int k = 0; k < K; k++)
		{
			_mm_free(mmu[k]);
		}
	}
}

//isUseLocalMu=false: guide and mu for alpha
template<int use_fmath>
void ConstantTimeHDGF_InterpolationSingle::computeAlpha(const std::vector<cv::Mat>& guide, const int k)
{
	bool isROI = true;

	const float coeff = float(-1.0 / (2.0 * sigma_range * sigma_range));
	const __m256 mcoef = _mm256_set1_ps(coeff);
	const int imsize = img_size.area() / 8;
	if (guide_channels == 1)
	{
		__m256* gptr = (__m256*)guide[0].ptr<float>();
		__m256* a_ptr = (__m256*)alpha[0].ptr<float>();
		const float* muPtr = mu.ptr<float>(k);
		__m256 mmu = _mm256_set1_ps(muPtr[0]);

		for (int n = 0; n < imsize; n++)
		{
			__m256 msub = _mm256_sub_ps(*gptr++, mmu);
			__m256 mdiff = _mm256_mul_ps(msub, msub);

			*a_ptr++ = v_exp_ps<use_fmath>(_mm256_mul_ps(mcoef, mdiff));
		}
	}
	else if (guide_channels == 2)
	{
		const __m256* im0 = (__m256*)guide[0].ptr<float>();
		const __m256* im1 = (__m256*)guide[1].ptr<float>();
		__m256* aptr = (__m256*)alpha[0].ptr<float>();

		const __m256 mc0 = _mm256_set1_ps(mu.at<cv::Vec2f>(k)[0]);
		const __m256 mc1 = _mm256_set1_ps(mu.at<cv::Vec2f>(k)[1]);

		for (int n = 0; n < imsize; n++)
		{
			__m256 msub = _mm256_sub_ps(*im0++, mc0);
			__m256 mdiff = _mm256_mul_ps(msub, msub);
			msub = _mm256_sub_ps(*im1++, mc1);
			mdiff = _mm256_fmadd_ps(msub, msub, mdiff);

			*aptr++ = v_exp_ps<use_fmath>(_mm256_mul_ps(mcoef, mdiff));
		}
	}
	else if (guide_channels == 3)
	{
		if (isROI)
		{
			const __m256 mc0 = _mm256_set1_ps(mu.at<cv::Vec3f>(k)[0]);
			const __m256 mc1 = _mm256_set1_ps(mu.at<cv::Vec3f>(k)[1]);
			const __m256 mc2 = _mm256_set1_ps(mu.at<cv::Vec3f>(k)[2]);

			for (int y = boundaryLength; y < img_size.height - boundaryLength; y++)
			{
				const __m256* im0 = (__m256*)guide[0].ptr<float>(y, boundaryLength);
				const __m256* im1 = (__m256*)guide[1].ptr<float>(y, boundaryLength);
				const __m256* im2 = (__m256*)guide[2].ptr<float>(y, boundaryLength);
				__m256* aptr = (__m256*)alpha[0].ptr<float>(y, boundaryLength);

				for (int x = boundaryLength; x < img_size.width - boundaryLength; x += 8)
				{
					__m256 msub = _mm256_sub_ps(*im0++, mc0);
					__m256 mdiff = _mm256_mul_ps(msub, msub);
					msub = _mm256_sub_ps(*im1++, mc1);
					mdiff = _mm256_fmadd_ps(msub, msub, mdiff);
					msub = _mm256_sub_ps(*im2++, mc2);
					mdiff = _mm256_fmadd_ps(msub, msub, mdiff);

					*aptr++ = v_exp_ps<use_fmath>(_mm256_mul_ps(mcoef, mdiff));
				}
			}
		}
		else
		{
			const __m256* im0 = (__m256*)guide[0].ptr<float>();
			const __m256* im1 = (__m256*)guide[1].ptr<float>();
			const __m256* im2 = (__m256*)guide[2].ptr<float>();
			__m256* aptr = (__m256*)alpha[0].ptr<float>();

			const __m256 mc0 = _mm256_set1_ps(mu.at<cv::Vec3f>(k)[0]);
			const __m256 mc1 = _mm256_set1_ps(mu.at<cv::Vec3f>(k)[1]);
			const __m256 mc2 = _mm256_set1_ps(mu.at<cv::Vec3f>(k)[2]);

			for (int n = 0; n < imsize; n++)
			{
				__m256 msub = _mm256_sub_ps(*im0++, mc0);
				__m256 mdiff = _mm256_mul_ps(msub, msub);
				msub = _mm256_sub_ps(*im1++, mc1);
				mdiff = _mm256_fmadd_ps(msub, msub, mdiff);
				msub = _mm256_sub_ps(*im2++, mc2);
				mdiff = _mm256_fmadd_ps(msub, msub, mdiff);

				*aptr++ = v_exp_ps<use_fmath>(_mm256_mul_ps(mcoef, mdiff));
			}
		}
	}
	else //n-dimensional signal
	{
		cv::AutoBuffer<__m256*> gptr(guide_channels);
		cv::AutoBuffer<__m256> mguide(guide_channels);
		for (int c = 0; c < guide_channels; c++) gptr[c] = (__m256*)guide[c].ptr<float>();
		__m256* a_ptr = (__m256*)alpha[0].ptr<float>();
		const float* muPtr = mu.ptr<float>(k);
		AutoBuffer<__m256> mmu(guide_channels);
		for (int c = 0; c < guide_channels; c++)
		{
			mmu[c] = _mm256_set1_ps(muPtr[c]);
		}

		for (int n = 0; n < imsize; n++)
		{
			__m256 mdiff;
			{
				__m256 msub = _mm256_sub_ps(*gptr[0]++, mmu[0]);
				mdiff = _mm256_mul_ps(msub, msub);
			}
			for (int c = 1; c < guide_channels; c++)
			{
				__m256 msub = _mm256_sub_ps(*gptr[c]++, mmu[c]);
				mdiff = _mm256_fmadd_ps(msub, msub, mdiff);
			}

			*a_ptr++ = v_exp_ps<use_fmath>(_mm256_mul_ps(mcoef, mdiff));
		}
	}
}

template<int use_fmath>
void ConstantTimeHDGF_InterpolationSingle::computeW(const std::vector<cv::Mat>& src, std::vector<cv::Mat>& vecW)
{
	const int size = src[0].size().area();
	const float coeff = float(-1.0 / (2.0 * sigma_range * sigma_range));
	const __m256 mcoef = _mm256_set1_ps(coeff);
	const __m256 mlambda = _mm256_set1_ps(-lambda);
	cv::AutoBuffer<float*> w_ptr(K);
	for (int k = 0; k < K; k++)
	{
		w_ptr[k] = vecW[k].ptr<float>();
	}

	const int method = 0;//nk-loop-hard
	//const int method = 1;//nk-loop-soft
	if (method == 0)
	{
		__m256 margclip = _mm256_set1_ps(float(sigma_range * 6.0 * sigma_range * 6.0));
		const float* im0 = src[0].ptr<float>();
		const float* im1 = src[1].ptr<float>();
		const float* im2 = src[2].ptr<float>();

		if (guide_channels == 3)
		{
			for (int n = 0; n < size; n += 8)
			{
				const __m256 mimage0 = _mm256_load_ps(im0 + n);
				const __m256 mimage1 = _mm256_load_ps(im1 + n);
				const __m256 mimage2 = _mm256_load_ps(im2 + n);

				__m256 mdiffmax = _mm256_set1_ps(FLT_MAX);
				__m256 argment = _mm256_set1_ps(0.f);

				for (int k = 0; k < K; k++)
				{
					const float* muPtr = mu.ptr<float>(k);
					const __m256 mcenter0 = _mm256_set1_ps(muPtr[0]);
					const __m256 mcenter1 = _mm256_set1_ps(muPtr[1]);
					const __m256 mcenter2 = _mm256_set1_ps(muPtr[2]);

					__m256 msub = _mm256_sub_ps(mimage0, mcenter0);
					__m256 mdiff = _mm256_mul_ps(msub, msub);
					msub = _mm256_sub_ps(mimage1, mcenter1);
					mdiff = _mm256_fmadd_ps(msub, msub, mdiff);
					msub = _mm256_sub_ps(mimage2, mcenter2);
					mdiff = _mm256_fmadd_ps(msub, msub, mdiff);
					mdiff = _mm256_min_ps(margclip, mdiff);

					_mm256_argmin_ps(mdiff, mdiffmax, argment, float(k));
				}
				for (int k = 0; k < K; k++)
				{
					//_mm256_store_ps(w_ptr[k] + n, _mm256_blendv_ps(_mm256_set1_ps(FLT_MIN), _mm256_set1_ps(1.f), _mm256_cmp_ps(argment, _mm256_set1_ps(k), 0)));
					//_mm256_store_ps(w_ptr[k] + n, _mm256_blendv_ps(_mm256_setzero_ps(), _mm256_set1_ps(1.f), _mm256_cmp_ps(argment, _mm256_set1_ps(k), 0)));
					_mm256_store_ps(w_ptr[k] + n, _mm256_andnot_ps(_mm256_set1_ps(1.f), _mm256_cmp_ps(argment, _mm256_set1_ps(float(k)), 0)));
				}
			}
		}
		else
		{
			for (int n = 0; n < size; n += 8)
			{
				const __m256 mimage0 = _mm256_load_ps(im0 + n);
				const __m256 mimage1 = _mm256_load_ps(im1 + n);
				const __m256 mimage2 = _mm256_load_ps(im2 + n);

				__m256 mdiffmax = _mm256_set1_ps(FLT_MAX);
				__m256 argment = _mm256_set1_ps(0.f);

				for (int k = 0; k < K; k++)
				{
					const float* muPtr = mu.ptr<float>(k);
					__m256 mdiff = _mm256_setzero_ps();
					for (int c = 0; c < guide_channels; c++)
					{
						__m256 msub = _mm256_sub_ps(mimage0, _mm256_set1_ps(muPtr[c]));
						mdiff = _mm256_fmadd_ps(msub, msub, mdiff);
					}
					_mm256_argmin_ps(mdiff, mdiffmax, argment, (float)k);
				}
				for (int k = 0; k < K; k++)
				{
					//_mm256_store_ps(w_ptr[k] + n, _mm256_blendv_ps(_mm256_set1_ps(FLT_MIN), _mm256_set1_ps(1.f), _mm256_cmp_ps(argment, _mm256_set1_ps(k), 0)));
					_mm256_store_ps(w_ptr[k] + n, _mm256_blendv_ps(_mm256_setzero_ps(), _mm256_set1_ps(1.f), _mm256_cmp_ps(argment, _mm256_set1_ps(float(k)), 0)));
				}
			}
		}
	}
	else if (method == 1)
	{
		__m256 margclip = _mm256_set1_ps(float(sigma_range * 6.0 * sigma_range * 6.0));
		const float* im0 = src[0].ptr<float>();
		const float* im1 = src[1].ptr<float>();
		const float* im2 = src[2].ptr<float>();
		for (int n = 0; n < size; n += 8)
		{
			const __m256 mimage0 = _mm256_load_ps(im0 + n);
			const __m256 mimage1 = _mm256_load_ps(im1 + n);
			const __m256 mimage2 = _mm256_load_ps(im2 + n);
			__m256 malpha_sum = _mm256_setzero_ps();
			cv::AutoBuffer<__m256> mvalpha(K);
			for (int k = 0; k < K; k++)
			{
				const __m256 mcenter0 = _mm256_set1_ps(mu.at<cv::Vec3f>(k)[0]);
				const __m256 mcenter1 = _mm256_set1_ps(mu.at<cv::Vec3f>(k)[1]);
				const __m256 mcenter2 = _mm256_set1_ps(mu.at<cv::Vec3f>(k)[2]);

				__m256 msub = _mm256_sub_ps(mimage0, mcenter0);
				__m256 mdiff = _mm256_mul_ps(msub, msub);
				msub = _mm256_sub_ps(mimage1, mcenter1);
				mdiff = _mm256_fmadd_ps(msub, msub, mdiff);
				msub = _mm256_sub_ps(mimage2, mcenter2);
				mdiff = _mm256_fmadd_ps(msub, msub, mdiff);

				mdiff = _mm256_min_ps(margclip, mdiff);

				const __m256 malpha = v_exp_ps<use_fmath>(_mm256_mul_ps(mlambda, _mm256_sqrt_ps(mdiff)));//Laplacian
				//const __m256 malpha = v_exp_ps<use_fmath>(_mm256_mul_ps(mlambda, mdiff));//Gaussian
				mvalpha[k] = malpha;
				malpha_sum = _mm256_add_ps(malpha_sum, malpha);
			}
			for (int k = 0; k < K; k++)
			{
				float* w_ptr = vecW[k].ptr<float>();
				//_mm256_store_ps(w_ptr + n, _mm256_blendv_ps(_mm256_set1_ps(FLT_MIN), _mm256_set1_ps(1.f), _mm256_cmp_ps(argment, _mm256_set1_ps(k), 0)));
				_mm256_store_ps(w_ptr + n, _mm256_div_ps(mvalpha[k], malpha_sum));
			}
		}
	}
}

void ConstantTimeHDGF_InterpolationSingle::mergeNumerDenomMat(vector<Mat>& dest, const int k, const int upsampleSize)
{
	const int intermethod = INTER_LINEAR;
	//const int intermethod = INTER_CUBIC;
	const int wk = isWRedunductLoadDecomposition ? 0 : k;

	if (upsampleSize != 1)
	{
		for (int c = 0; c < channels; c++)
		{
			resize(split_inter[c], NumerDenomRes[c], Size(), downSampleImage, downSampleImage, intermethod);
		}
		resize(vecW[wk], NumerDenomRes[channels], Size(), downSampleImage, downSampleImage, intermethod);
		//cp::upsampleLinear(split_inter[0], NumerDenomRes[0], downSampleImage);
		//cp::upsampleLinear(split_inter[1], NumerDenomRes[1], downSampleImage);
		//cp::upsampleLinear(split_inter[2], NumerDenomRes[2], downSampleImage);
		//cp::upsampleLinear(vecW[wk], NumerDenomRes[3], downSampleImage);

		for (int c = 0; c < channels; c++)
		{
			dest[c] = NumerDenomRes[c];
		}
		dest[channels] = NumerDenomRes[channels];
	}
	else
	{
		for (int c = 0; c < channels; c++)
		{
			dest[c] = split_inter[c];
		}
		dest[channels] = vecW[wk];
	}
}

template<int use_fmath>
void ConstantTimeHDGF_InterpolationSingle::mergeRecomputeAlphaForUsingMu(std::vector<cv::Mat>& src, const int k, const bool isInit)
{
	if (isJoint)
	{
		std::cout << "ConstantTimeHDGF_InterpolationSingle::recomputeAlpha must be src==guide" << std::endl;
		CV_Assert(!isJoint);
	}

	const int w = src[0].cols;
	const int h = src[0].rows;

	const float coeff = float(-1.0 / (2.0 * sigma_range * sigma_range));
	const __m256 mcoef = _mm256_set1_ps(coeff);

	const int wk = isWRedunductLoadDecomposition ? 0 : k;

	vector<Mat> intermat(channels + 1);
	mergeNumerDenomMat(intermat, k, downSampleImage);

	if (channels == 3)
	{
		if (isInit)
		{
			for (int y = boundaryLength; y < h - boundaryLength; y++)
			{
				const __m256* src0 = (const __m256*)src[0].ptr<float>(y, boundaryLength);
				const __m256* src1 = (const __m256*)src[1].ptr<float>(y, boundaryLength);
				const __m256* src2 = (const __m256*)src[2].ptr<float>(y, boundaryLength);
				const __m256* inter0 = (const __m256*)intermat[0].ptr<float>(y, boundaryLength);
				const __m256* inter1 = (const __m256*)intermat[1].ptr<float>(y, boundaryLength);
				const __m256* inter2 = (const __m256*)intermat[2].ptr<float>(y, boundaryLength);
				const __m256* interw = (const __m256*)intermat[3].ptr<float>(y, boundaryLength);

				__m256* numer0 = (__m256*)numer[0].ptr<float>(y, boundaryLength);
				__m256* numer1 = (__m256*)numer[1].ptr<float>(y, boundaryLength);
				__m256* numer2 = (__m256*)numer[2].ptr<float>(y, boundaryLength);
				__m256* denom_ = (__m256*)denom.ptr<float>(y, boundaryLength);

				for (int x = boundaryLength; x < w - boundaryLength; x += 8)
				{
					const __m256 norm = _mm256_div_avoidzerodiv_ps(_mm256_set1_ps(1.f), *interw);

					__m256 msub = _mm256_fnmadd_ps(*inter0, norm, *src0++);
					__m256 mdiff = _mm256_mul_ps(msub, msub);
					msub = _mm256_fnmadd_ps(*inter1, norm, *src1++);
					mdiff = _mm256_fmadd_ps(msub, msub, mdiff);
					msub = _mm256_fnmadd_ps(*inter2, norm, *src2++);
					mdiff = _mm256_fmadd_ps(msub, msub, mdiff);

					const __m256 malpha = v_exp_ps<use_fmath>(_mm256_mul_ps(mcoef, mdiff));

					*numer0++ = _mm256_mul_ps(malpha, *inter0++);
					*numer1++ = _mm256_mul_ps(malpha, *inter1++);
					*numer2++ = _mm256_mul_ps(malpha, *inter2++);
					*denom_++ = _mm256_mul_ps(malpha, *interw++);
				}
			}
		}
		else
		{
			for (int y = boundaryLength; y < h - boundaryLength; y++)
			{
				const __m256* src0 = (const __m256*)src[0].ptr<float>(y, boundaryLength);
				const __m256* src1 = (const __m256*)src[1].ptr<float>(y, boundaryLength);
				const __m256* src2 = (const __m256*)src[2].ptr<float>(y, boundaryLength);
				const __m256* inter0 = (const __m256*)intermat[0].ptr<float>(y, boundaryLength);
				const __m256* inter1 = (const __m256*)intermat[1].ptr<float>(y, boundaryLength);
				const __m256* inter2 = (const __m256*)intermat[2].ptr<float>(y, boundaryLength);
				const __m256* interw = (const __m256*)intermat[3].ptr<float>(y, boundaryLength);

				__m256* numer0 = (__m256*)numer[0].ptr<float>(y, boundaryLength);
				__m256* numer1 = (__m256*)numer[1].ptr<float>(y, boundaryLength);
				__m256* numer2 = (__m256*)numer[2].ptr<float>(y, boundaryLength);
				__m256* denom_ = (__m256*)denom.ptr<float>(y, boundaryLength);

				for (int x = boundaryLength; x < w - boundaryLength; x += 8)
				{
					const __m256 norm = _mm256_div_avoidzerodiv_ps(_mm256_set1_ps(1.f), *interw);

					__m256 msub = _mm256_fnmadd_ps(*inter0, norm, *src0++);
					__m256 mdiff = _mm256_mul_ps(msub, msub);
					msub = _mm256_fnmadd_ps(*inter1, norm, *src1++);
					mdiff = _mm256_fmadd_ps(msub, msub, mdiff);
					msub = _mm256_fnmadd_ps(*inter2, norm, *src2++);
					mdiff = _mm256_fmadd_ps(msub, msub, mdiff);

					const __m256 malpha = v_exp_ps<use_fmath>(_mm256_mul_ps(mcoef, mdiff));

					*numer0++ = _mm256_fmadd_ps(malpha, *inter0++, *numer0);
					*numer1++ = _mm256_fmadd_ps(malpha, *inter1++, *numer1);
					*numer2++ = _mm256_fmadd_ps(malpha, *inter2++, *numer2);
					*denom_++ = _mm256_fmadd_ps(malpha, *interw++, *denom_);
				}
			}
		}
	}
	else
	{
		AutoBuffer<const __m256*> msrc(channels);
		AutoBuffer<const __m256*> minter(channels);
		AutoBuffer<__m256*> mnumer(channels);
		if (isInit)
		{
			for (int y = boundaryLength; y < h - boundaryLength; y++)
			{
				for (int c = 0; c < channels; c++) msrc[c] = (const __m256*)src[c].ptr<float>(y, boundaryLength);
				for (int c = 0; c < channels; c++) minter[c] = (const __m256*)intermat[c].ptr<float>(y, boundaryLength);
				const __m256* minterw = (const __m256*)intermat[channels].ptr<float>(y, boundaryLength);

				for (int c = 0; c < channels; c++)  mnumer[c] = (__m256*)numer[c].ptr<float>(y, boundaryLength);
				__m256* mdenom = (__m256*)denom.ptr<float>(y, boundaryLength);

				for (int x = boundaryLength; x < w - boundaryLength; x += 8)
				{
					const __m256 norm = _mm256_div_avoidzerodiv_ps(_mm256_set1_ps(1.f), *minterw);

					__m256 msub = _mm256_fnmadd_ps(*minter[0], norm, *msrc[0]++);
					__m256 mdiff = _mm256_mul_ps(msub, msub);
					for (int c = 1; c < channels; c++)
					{
						msub = _mm256_fnmadd_ps(*minter[c], norm, *msrc[c]++);
						mdiff = _mm256_fmadd_ps(msub, msub, mdiff);
					}

					const __m256 malpha = v_exp_ps<use_fmath>(_mm256_mul_ps(mcoef, mdiff));

					for (int c = 0; c < channels; c++)
					{
						*mnumer[c]++ = _mm256_mul_ps(malpha, *minter[c]++);
					}
					*mdenom++ = _mm256_mul_ps(malpha, *minterw++);
				}
			}
		}
		else
		{
			for (int y = boundaryLength; y < h - boundaryLength; y++)
			{
				for (int c = 0; c < channels; c++) msrc[c] = (const __m256*)src[c].ptr<float>(y, boundaryLength);
				for (int c = 0; c < channels; c++) minter[c] = (const __m256*)intermat[c].ptr<float>(y, boundaryLength);
				const __m256* minterw = (const __m256*)intermat[channels].ptr<float>(y, boundaryLength);

				for (int c = 0; c < channels; c++)  mnumer[c] = (__m256*)numer[c].ptr<float>(y, boundaryLength);
				__m256* mdenom = (__m256*)denom.ptr<float>(y, boundaryLength);

				for (int x = boundaryLength; x < w - boundaryLength; x += 8)
				{
					const __m256 norm = _mm256_div_avoidzerodiv_ps(_mm256_set1_ps(1.f), *minterw);

					__m256 msub = _mm256_fnmadd_ps(*minter[0], norm, *msrc[0]++);
					__m256 mdiff = _mm256_mul_ps(msub, msub);
					for (int c = 1; c < channels; c++)
					{
						msub = _mm256_fnmadd_ps(*minter[c], norm, *msrc[c]++);
						mdiff = _mm256_fmadd_ps(msub, msub, mdiff);
					}

					const __m256 malpha = v_exp_ps<use_fmath>(_mm256_mul_ps(mcoef, mdiff));

					for (int c = 0; c < channels; c++)
					{
						*mnumer[c]++ = _mm256_fmadd_ps(malpha, *minter[c]++, *mnumer[c]);
					}
					*mdenom++ = _mm256_fmadd_ps(malpha, *minterw++, *mdenom);
				}
			}
		}
	}
}

template<int use_fmath, int channels, int guide_channels>
void ConstantTimeHDGF_InterpolationSingle::mergeRecomputeAlphaForUsingMuPCA(std::vector<cv::Mat>& guide, const int k, const bool isInit)
{
	const int w = guide[0].cols;
	const int h = guide[0].rows;

	const float coeff = float(-1.0 / (2.0 * sigma_range * sigma_range));
	const __m256 mcoef = _mm256_set1_ps(coeff);

	const int wk = isWRedunductLoadDecomposition ? 0 : k;
	AutoBuffer<AutoBuffer<__m256>> p(guide_channels);
	for (int c = 0; c < guide_channels; c++)
	{
		p.resize(channels);
		for (int cc = 0; cc < channels; cc++)
		{
			p[c][cc] = _mm256_set1_ps(projectionMatrix.at<float>(c, cc));
		}
	}

	vector<Mat> intermat(channels + 1);
	mergeNumerDenomMat(intermat, k, downSampleImage);

	if (channels == 3)
	{
		AutoBuffer<const __m256*> mguide(guide_channels);
		AutoBuffer<__m256> mpca(guide_channels);

		if (isInit)
		{
			for (int y = boundaryLength; y < h - boundaryLength; y++)
			{
				for (int c = 0; c < guide_channels; c++) mguide[c] = (const __m256*)guide[c].ptr<float>(y, boundaryLength);
				const __m256* minter0 = (const __m256*)intermat[0].ptr<float>(y, boundaryLength);
				const __m256* minter1 = (const __m256*)intermat[1].ptr<float>(y, boundaryLength);
				const __m256* minter2 = (const __m256*)intermat[2].ptr<float>(y, boundaryLength);
				const __m256* minterw = (const __m256*)intermat[3].ptr<float>(y, boundaryLength);
				__m256* mnumer0 = (__m256*)numer[0].ptr<float>(y, boundaryLength);
				__m256* mnumer1 = (__m256*)numer[1].ptr<float>(y, boundaryLength);
				__m256* mnumer2 = (__m256*)numer[2].ptr<float>(y, boundaryLength);
				__m256* mdenom = (__m256*)denom.ptr<float>(y, boundaryLength);

				for (int x = boundaryLength; x < w - boundaryLength; x += 8)
				{
					const __m256 norm = _mm256_div_avoidzerodiv_ps(_mm256_set1_ps(1.f), *minterw);

					for (int c = 0; c < guide_channels; c++)
					{
						mpca[c] = _mm256_mul_ps(*minter0, p[c][0]);
						mpca[c] = _mm256_fmadd_ps(*minter1, p[c][1], mpca[c]);
						mpca[c] = _mm256_fmadd_ps(*minter2, p[c][2], mpca[c]);
					}

					__m256 msub = _mm256_fnmadd_ps(mpca[0], norm, *mguide[0]++);
					__m256 mdiff = _mm256_mul_ps(msub, msub);
					for (int c = 1; c < guide_channels; c++)
					{
						msub = _mm256_fnmadd_ps(mpca[c], norm, *mguide[c]++);
						mdiff = _mm256_fmadd_ps(msub, msub, mdiff);
					}

					const __m256 malpha = v_exp_ps<use_fmath>(_mm256_mul_ps(mcoef, mdiff));
					*mnumer0++ = _mm256_mul_ps(malpha, *minter0++);
					*mnumer1++ = _mm256_mul_ps(malpha, *minter1++);
					*mnumer2++ = _mm256_mul_ps(malpha, *minter2++);
					*mdenom++ = _mm256_mul_ps(malpha, *minterw++);
				}
			}
		}
		else
		{
			for (int y = boundaryLength; y < h - boundaryLength; y++)
			{
				for (int c = 0; c < guide_channels; c++) mguide[c] = (const __m256*)guide[c].ptr<float>(y, boundaryLength);

				const __m256* minter0 = (const __m256*)intermat[0].ptr<float>(y, boundaryLength);
				const __m256* minter1 = (const __m256*)intermat[1].ptr<float>(y, boundaryLength);
				const __m256* minter2 = (const __m256*)intermat[2].ptr<float>(y, boundaryLength);
				const __m256* minterw = (const __m256*)intermat[3].ptr<float>(y, boundaryLength);
				__m256* mnumer0 = (__m256*)numer[0].ptr<float>(y, boundaryLength);
				__m256* mnumer1 = (__m256*)numer[1].ptr<float>(y, boundaryLength);
				__m256* mnumer2 = (__m256*)numer[2].ptr<float>(y, boundaryLength);
				__m256* mdenom = (__m256*)denom.ptr<float>(y, boundaryLength);

				for (int x = boundaryLength; x < w - boundaryLength; x += 8)
				{
					const __m256 norm = _mm256_div_avoidzerodiv_ps(_mm256_set1_ps(1.f), *minterw);

					for (int c = 0; c < guide_channels; c++)
					{
						mpca[c] = _mm256_mul_ps(*minter0, p[c][0]);
						mpca[c] = _mm256_fmadd_ps(*minter1, p[c][1], mpca[c]);
						mpca[c] = _mm256_fmadd_ps(*minter2, p[c][2], mpca[c]);
					}

					__m256 msub = _mm256_fnmadd_ps(mpca[0], norm, *mguide[0]++);
					__m256 mdiff = _mm256_mul_ps(msub, msub);
					for (int c = 1; c < guide_channels; c++)
					{
						msub = _mm256_fnmadd_ps(mpca[c], norm, *mguide[c]++);
						mdiff = _mm256_fmadd_ps(msub, msub, mdiff);
					}

					const __m256 malpha = v_exp_ps<use_fmath>(_mm256_mul_ps(mcoef, mdiff));
					*mnumer0++ = _mm256_fmadd_ps(malpha, *minter0++, *mnumer0);
					*mnumer1++ = _mm256_fmadd_ps(malpha, *minter1++, *mnumer1);
					*mnumer2++ = _mm256_fmadd_ps(malpha, *minter2++, *mnumer2);
					*mdenom++ = _mm256_fmadd_ps(malpha, *minterw++, *mdenom);
				}
			}
		}
	}
	else
	{
		AutoBuffer<const __m256*> mguide(guide_channels);
		AutoBuffer<const __m256*> minter(channels);
		AutoBuffer<__m256*> mnumer(channels);
		AutoBuffer<__m256> mpca(guide_channels);

		if (isInit)
		{
			for (int y = boundaryLength; y < h - boundaryLength; y++)
			{
				for (int c = 0; c < guide_channels; c++) mguide[c] = (const __m256*)guide[c].ptr<float>(y, boundaryLength);
				for (int c = 0; c < channels; c++) minter[c] = (const __m256*)intermat[c].ptr<float>(y, boundaryLength);
				const __m256* minterw = (const __m256*)intermat[channels].ptr<float>(y, boundaryLength);
				for (int c = 0; c < channels; c++)  mnumer[c] = (__m256*)numer[c].ptr<float>(y, boundaryLength);
				__m256* mdenom = (__m256*)denom.ptr<float>(y, boundaryLength);

				for (int x = boundaryLength; x < w - boundaryLength; x += 8)
				{
					const __m256 norm = _mm256_div_avoidzerodiv_ps(_mm256_set1_ps(1.f), *minterw);

					for (int c = 0; c < guide_channels; c++)
					{
						mpca[c] = _mm256_mul_ps(*minter[0], p[c][0]);
						for (int cc = 1; cc < channels; cc++)
						{
							mpca[c] = _mm256_fmadd_ps(*minter[cc], p[c][cc], mpca[c]);
						}
					}

					__m256 msub = _mm256_fnmadd_ps(mpca[0], norm, *mguide[0]++);
					__m256 mdiff = _mm256_mul_ps(msub, msub);
					for (int c = 1; c < guide_channels; c++)
					{
						msub = _mm256_fnmadd_ps(mpca[c], norm, *mguide[c]++);
						mdiff = _mm256_fmadd_ps(msub, msub, mdiff);
					}

					const __m256 malpha = v_exp_ps<use_fmath>(_mm256_mul_ps(mcoef, mdiff));

					for (int c = 0; c < channels; c++)
					{
						*mnumer[c]++ = _mm256_mul_ps(malpha, *minter[c]++);
					}
					*mdenom++ = _mm256_mul_ps(malpha, *minterw++);
				}
			}
		}
		else
		{
			for (int y = boundaryLength; y < h - boundaryLength; y++)
			{
				for (int c = 0; c < guide_channels; c++) mguide[c] = (const __m256*)guide[c].ptr<float>(y, boundaryLength);
				for (int c = 0; c < channels; c++) minter[c] = (const __m256*)intermat[c].ptr<float>(y, boundaryLength);
				const __m256* minterw = (const __m256*)intermat[channels].ptr<float>(y, boundaryLength);
				for (int c = 0; c < channels; c++)  mnumer[c] = (__m256*)numer[c].ptr<float>(y, boundaryLength);
				__m256* mdenom = (__m256*)denom.ptr<float>(y, boundaryLength);

				for (int x = boundaryLength; x < w - boundaryLength; x += 8)
				{
					const __m256 norm = _mm256_div_avoidzerodiv_ps(_mm256_set1_ps(1.f), *minterw);

					for (int c = 0; c < guide_channels; c++)
					{
						mpca[c] = _mm256_mul_ps(*minter[0], p[c][0]);
						for (int cc = 1; cc < channels; cc++)
						{
							mpca[c] = _mm256_fmadd_ps(*minter[cc], p[c][cc], mpca[c]);
						}
					}

					__m256 msub = _mm256_fnmadd_ps(mpca[0], norm, *mguide[0]++);
					__m256 mdiff = _mm256_mul_ps(msub, msub);
					for (int c = 1; c < guide_channels; c++)
					{
						msub = _mm256_fnmadd_ps(mpca[c], norm, *mguide[c]++);
						mdiff = _mm256_fmadd_ps(msub, msub, mdiff);
					}

					const __m256 malpha = v_exp_ps<use_fmath>(_mm256_mul_ps(mcoef, mdiff));

					for (int c = 0; c < channels; c++)
					{
						*mnumer[c]++ = _mm256_fmadd_ps(malpha, *minter[c]++, *mnumer[c]);
					}
					*mdenom++ = _mm256_fmadd_ps(malpha, *minterw++, *mdenom);
				}
			}
		}
	}
}

template<int use_fmath>
void ConstantTimeHDGF_InterpolationSingle::mergeRecomputeAlphaForUsingMuPCA(std::vector<cv::Mat>& guide, const int k, const bool isInit)
{
	const int w = guide[0].cols;
	const int h = guide[0].rows;

	const float coeff = float(-1.0 / (2.0 * sigma_range * sigma_range));
	const __m256 mcoef = _mm256_set1_ps(coeff);

	const int wk = isWRedunductLoadDecomposition ? 0 : k;
	AutoBuffer<AutoBuffer<__m256>> p(guide_channels);
	for (int c = 0; c < guide_channels; c++)
	{
		p.resize(channels);
		for (int cc = 0; cc < channels; cc++)
		{
			p[c][cc] = _mm256_set1_ps(projectionMatrix.at<float>(c, cc));
		}
	}

	vector<Mat> intermat(channels + 1);
	mergeNumerDenomMat(intermat, k, downSampleImage);

	AutoBuffer<const __m256*> mguide(guide_channels);
	AutoBuffer<const __m256*> minter(channels);
	AutoBuffer<__m256*> mnumer(channels);
	AutoBuffer<__m256> mpca(guide_channels);
	if (isInit)
	{
		for (int y = boundaryLength; y < h - boundaryLength; y++)
		{
			for (int c = 0; c < guide_channels; c++) mguide[c] = (const __m256*)guide[c].ptr<float>(y, boundaryLength);
			for (int c = 0; c < channels; c++) minter[c] = (const __m256*)intermat[c].ptr<float>(y, boundaryLength);
			const __m256* minterw = (const __m256*)intermat[channels].ptr<float>(y, boundaryLength);
			for (int c = 0; c < channels; c++)  mnumer[c] = (__m256*)numer[c].ptr<float>(y, boundaryLength);
			__m256* mdenom = (__m256*)denom.ptr<float>(y, boundaryLength);

			for (int x = boundaryLength; x < w - boundaryLength; x += 8)
			{
				const __m256 norm = _mm256_div_avoidzerodiv_ps(_mm256_set1_ps(1.f), *minterw);

				for (int c = 0; c < guide_channels; c++)
				{
					mpca[c] = _mm256_mul_ps(*minter[0], p[c][0]);
					for (int cc = 1; cc < channels; cc++)
					{
						mpca[c] = _mm256_fmadd_ps(*minter[cc], p[c][cc], mpca[c]);
					}
				}

				__m256 msub = _mm256_fnmadd_ps(mpca[0], norm, *mguide[0]++);
				__m256 mdiff = _mm256_mul_ps(msub, msub);
				for (int c = 1; c < guide_channels; c++)
				{
					msub = _mm256_fnmadd_ps(mpca[c], norm, *mguide[c]++);
					mdiff = _mm256_fmadd_ps(msub, msub, mdiff);
				}

				const __m256 malpha = v_exp_ps<use_fmath>(_mm256_mul_ps(mcoef, mdiff));

				for (int c = 0; c < channels; c++)
				{
					*mnumer[c]++ = _mm256_mul_ps(malpha, *minter[c]++);
				}
				*mdenom++ = _mm256_mul_ps(malpha, *minterw++);
			}
		}
	}
	else
	{
		for (int y = boundaryLength; y < h - boundaryLength; y++)
		{
			for (int c = 0; c < guide_channels; c++) mguide[c] = (const __m256*)guide[c].ptr<float>(y, boundaryLength);
			for (int c = 0; c < channels; c++) minter[c] = (const __m256*)intermat[c].ptr<float>(y, boundaryLength);
			const __m256* minterw = (const __m256*)intermat[channels].ptr<float>(y, boundaryLength);
			for (int c = 0; c < channels; c++)  mnumer[c] = (__m256*)numer[c].ptr<float>(y, boundaryLength);
			__m256* mdenom = (__m256*)denom.ptr<float>(y, boundaryLength);

			for (int x = boundaryLength; x < w - boundaryLength; x += 8)
			{
				const __m256 norm = _mm256_div_avoidzerodiv_ps(_mm256_set1_ps(1.f), *minterw);

				for (int c = 0; c < guide_channels; c++)
				{
					mpca[c] = _mm256_mul_ps(*minter[0], p[c][0]);
					for (int cc = 1; cc < channels; cc++)
					{
						mpca[c] = _mm256_fmadd_ps(*minter[cc], p[c][cc], mpca[c]);
					}
				}

				__m256 msub = _mm256_fnmadd_ps(mpca[0], norm, *mguide[0]++);
				__m256 mdiff = _mm256_mul_ps(msub, msub);
				for (int c = 1; c < guide_channels; c++)
				{
					msub = _mm256_fnmadd_ps(mpca[c], norm, *mguide[c]++);
					mdiff = _mm256_fmadd_ps(msub, msub, mdiff);
				}

				const __m256 malpha = v_exp_ps<use_fmath>(_mm256_mul_ps(mcoef, mdiff));

				for (int c = 0; c < channels; c++)
				{
					*mnumer[c]++ = _mm256_fmadd_ps(malpha, *minter[c]++, *mnumer[c]);
				}
				*mdenom++ = _mm256_fmadd_ps(malpha, *minterw++, *mdenom);
			}
		}
	}
}

void ConstantTimeHDGF_InterpolationSingle::merge(const int k, const bool isInit)
{
	//merge
	const int wk = (isUsePrecomputedWforeachK) ? k : 0;
	vector<Mat> intermat(channels + 1);
	mergeNumerDenomMat(intermat, k, downSampleImage);

	const bool isROI = true;
	if (isInit)
	{
		if (channels == 1)
		{
			for (int y = boundaryLength; y < img_size.height - boundaryLength; y++)
			{
				const __m256* inter0 = (__m256*)intermat[0].ptr<float>(y, boundaryLength);
				const __m256* interw = (__m256*)intermat[1].ptr<float>(y, boundaryLength);
				const __m256* alpha_ptr = (__m256*)alpha[wk].ptr<float>(y, boundaryLength);

				__m256* numer0 = (__m256*)numer[0].ptr<float>(y, boundaryLength);
				__m256* denom_ = (__m256*)denom.ptr<float>(y, boundaryLength);

				for (int x = boundaryLength; x < img_size.width - boundaryLength; x += 8)
				{
					const __m256 malpha = *alpha_ptr++;
					*numer0++ = _mm256_mul_ps(malpha, *inter0++);
					*denom_++ = _mm256_mul_ps(malpha, *interw++);
				}
			}
		}
		else if (channels == 3)
		{
			if (isROI)
			{
				for (int y = boundaryLength; y < img_size.height - boundaryLength; y++)
				{
					const __m256* inter0 = (__m256*)intermat[0].ptr<float>(y, boundaryLength);
					const __m256* inter1 = (__m256*)intermat[1].ptr<float>(y, boundaryLength);
					const __m256* inter2 = (__m256*)intermat[2].ptr<float>(y, boundaryLength);
					const __m256* interw = (__m256*)intermat[3].ptr<float>(y, boundaryLength);
					const __m256* alpha_ptr = (__m256*)alpha[wk].ptr<float>(y, boundaryLength);

					__m256* numer0 = (__m256*)numer[0].ptr<float>(y, boundaryLength);
					__m256* numer1 = (__m256*)numer[1].ptr<float>(y, boundaryLength);
					__m256* numer2 = (__m256*)numer[2].ptr<float>(y, boundaryLength);
					__m256* denom_ = (__m256*)denom.ptr<float>(y, boundaryLength);

					for (int x = boundaryLength; x < img_size.width - boundaryLength; x += 8)
					{
						const __m256 malpha = *alpha_ptr++;
						*numer0++ = _mm256_mul_ps(malpha, *inter0++);
						*numer1++ = _mm256_mul_ps(malpha, *inter1++);
						*numer2++ = _mm256_mul_ps(malpha, *inter2++);
						*denom_++ = _mm256_mul_ps(malpha, *interw++);
					}
				}
			}
			else
			{
				const int size8 = img_size.area() / 8;
				const __m256* inter0 = (__m256*)intermat[0].ptr<float>();
				const __m256* inter1 = (__m256*)intermat[1].ptr<float>();
				const __m256* inter2 = (__m256*)intermat[2].ptr<float>();
				const __m256* interw = (__m256*)intermat[3].ptr<float>();
				const __m256* alpha_ptr = (__m256*)alpha[wk].ptr<float>();

				__m256* numer0 = (__m256*)numer[0].ptr<float>();
				__m256* numer1 = (__m256*)numer[1].ptr<float>();
				__m256* numer2 = (__m256*)numer[2].ptr<float>();
				__m256* denom_ = (__m256*)denom.ptr<float>();

				for (int i = 0; i < size8; i++)
				{
					const __m256 malpha = *alpha_ptr++;
					*numer0++ = _mm256_mul_ps(malpha, *inter0++);
					*numer1++ = _mm256_mul_ps(malpha, *inter1++);
					*numer2++ = _mm256_mul_ps(malpha, *inter2++);
					*denom_++ = _mm256_mul_ps(malpha, *interw++);
				}
			}
		}
		else //n-dimensional signal
		{
			for (int y = boundaryLength; y < img_size.height - boundaryLength; y++)
			{
				AutoBuffer<const __m256*> minter(channels);
				AutoBuffer<__m256*> mnumer(channels);
				for (int c = 0; c < channels; c++)
				{
					minter[c] = (const __m256*)intermat[c].ptr<float>(y, boundaryLength);
					mnumer[c] = (__m256*)numer[c].ptr<float>(y, boundaryLength);
				}
				const __m256* interw = (__m256*)intermat[channels].ptr<float>(y, boundaryLength);
				const __m256* alpha_ptr = (__m256*)alpha[wk].ptr<float>(y, boundaryLength);
				__m256* denom_ = (__m256*)denom.ptr<float>(y, boundaryLength);

				for (int x = boundaryLength; x < img_size.width - boundaryLength; x += 8)
				{
					const __m256 malpha = *alpha_ptr++;
					for (int c = 0; c < channels; c++)
					{
						*mnumer[c]++ = _mm256_mul_ps(malpha, *minter[c]++);
					}
					*denom_++ = _mm256_mul_ps(malpha, *interw++);
				}
			}
		}
	}
	else
	{
		if (channels == 1)
		{
			for (int y = boundaryLength; y < img_size.height - boundaryLength; y++)
			{
				const __m256* inter0 = (__m256*)intermat[0].ptr<float>(y, boundaryLength);
				const __m256* interw = (__m256*)intermat[1].ptr<float>(y, boundaryLength);
				const __m256* alpha_ptr = (__m256*)alpha[wk].ptr<float>(y, boundaryLength);

				__m256* numer0 = (__m256*)numer[0].ptr<float>(y, boundaryLength);
				__m256* denom_ = (__m256*)denom.ptr<float>(y, boundaryLength);

				for (int x = boundaryLength; x < img_size.width - boundaryLength; x += 8)
				{
					const __m256 malpha = *alpha_ptr++;
					*numer0++ = _mm256_fmadd_ps(malpha, *inter0++, *numer0);
					*denom_++ = _mm256_fmadd_ps(malpha, *interw++, *denom_);
				}
			}
		}
		else if (channels == 3)
		{
			if (isROI)
			{
				for (int y = boundaryLength; y < img_size.height - boundaryLength; y++)
				{
					const __m256* inter0 = (const __m256*)intermat[0].ptr<float>(y, boundaryLength);
					const __m256* inter1 = (const __m256*)intermat[1].ptr<float>(y, boundaryLength);
					const __m256* inter2 = (const __m256*)intermat[2].ptr<float>(y, boundaryLength);
					const __m256* interw = (const __m256*)intermat[3].ptr<float>(y, boundaryLength);
					const __m256* alphap = (const __m256*)alpha[wk].ptr<float>(y, boundaryLength);

					__m256* numer0 = (__m256*)numer[0].ptr<float>(y, boundaryLength);
					__m256* numer1 = (__m256*)numer[1].ptr<float>(y, boundaryLength);
					__m256* numer2 = (__m256*)numer[2].ptr<float>(y, boundaryLength);
					__m256* denom_ = (__m256*)denom.ptr<float>(y, boundaryLength);

					for (int x = boundaryLength; x < img_size.width - boundaryLength; x += 8)
					{
						const __m256 malpha = *alphap++;
						*numer0++ = _mm256_fmadd_ps(malpha, *inter0++, *numer0);
						*numer1++ = _mm256_fmadd_ps(malpha, *inter1++, *numer1);
						*numer2++ = _mm256_fmadd_ps(malpha, *inter2++, *numer2);
						*denom_++ = _mm256_fmadd_ps(malpha, *interw++, *denom_);
					}
				}
			}
			else
			{
				const int size8 = img_size.area() / 8;
				const __m256* inter0 = (__m256*)intermat[0].ptr<float>();
				const __m256* inter1 = (__m256*)intermat[1].ptr<float>();
				const __m256* inter2 = (__m256*)intermat[2].ptr<float>();
				const __m256* interw = (__m256*)intermat[3].ptr<float>();
				const __m256* alphap = (__m256*)alpha[wk].ptr<float>();

				__m256* numer0 = (__m256*)numer[0].ptr<float>();
				__m256* numer1 = (__m256*)numer[1].ptr<float>();
				__m256* numer2 = (__m256*)numer[2].ptr<float>();
				__m256* denom_ = (__m256*)denom.ptr<float>();

				for (int i = 0; i < size8; i++)
				{
					const __m256 malpha = *alphap++;
					*numer0++ = _mm256_fmadd_ps(malpha, *inter0++, *numer0);
					*numer1++ = _mm256_fmadd_ps(malpha, *inter1++, *numer1);
					*numer2++ = _mm256_fmadd_ps(malpha, *inter2++, *numer2);
					*denom_++ = _mm256_fmadd_ps(malpha, *interw++, *denom_);
				}
			}
		}
		else //n-dimensional signal
		{
			for (int y = boundaryLength; y < img_size.height - boundaryLength; y++)
			{
				AutoBuffer<const __m256*> minter(channels);
				AutoBuffer<__m256*> mnumer(channels);
				for (int c = 0; c < channels; c++)
				{
					minter[c] = (const __m256*)intermat[c].ptr<float>(y, boundaryLength);
					mnumer[c] = (__m256*)numer[c].ptr<float>(y, boundaryLength);
				}
				const __m256* interw = (__m256*)intermat[channels].ptr<float>(y, boundaryLength);
				const __m256* alpha_ptr = (__m256*)alpha[wk].ptr<float>(y, boundaryLength);
				__m256* denom_ = (__m256*)denom.ptr<float>(y, boundaryLength);

				for (int x = boundaryLength; x < img_size.width - boundaryLength; x += 8)
				{
					const __m256 malpha = *alpha_ptr++;
					for (int c = 0; c < channels; c++)
					{
						*mnumer[c] = _mm256_fmadd_ps(malpha, *minter[c]++, *mnumer[c]);
						*mnumer[c]++;
					}
					*denom_ = _mm256_fmadd_ps(malpha, *interw++, *denom_);
					*denom_++;
				}
			}
		}
	}
}

void ConstantTimeHDGF_InterpolationSingle::split_blur_merge(const int k, const bool isInit, const bool isUseFmath, const bool isUseLSP)
{
	const Size size = img_size / downSampleImage;
	const int imsize = size.area();
	const int IMSIZE8 = imsize / 8;

	if (isUsePrecomputedWforeachK)
	{
		float* vecw_ptr = vecW[k].ptr<float>();

		if (isUseLSP)
		{
			float* mask_ptr = blendLSPMask.ptr<float>();

			float* src0 = nullptr;
			float* src1 = nullptr;
			float* src2 = nullptr;
			src0 = vsrc[0].ptr<float>();//for gray and color
			if (channels == 3)//for color
			{
				src1 = vsrc[1].ptr<float>();
				src2 = vsrc[2].ptr<float>();
			}

			float* inter0 = nullptr;
			float* inter1 = nullptr;
			float* inter2 = nullptr;
			inter0 = split_inter[0].ptr<float>();//for gray and color
			if (channels == 3)//for color
			{
				inter1 = split_inter[1].ptr<float>();
				inter2 = split_inter[2].ptr<float>();
			}

			//split
			for (int n = 0; n < img_size.area(); n += 8)
			{
				__m256 mvecw = _mm256_load_ps(vecw_ptr + n);
				__m256 msrc0 = _mm256_load_ps(src0 + n);
				__m256 msrc1 = _mm256_load_ps(src1 + n);
				__m256 msrc2 = _mm256_load_ps(src2 + n);

				_mm256_store_ps(inter0 + n, _mm256_mul_ps(mvecw, msrc0));
				_mm256_store_ps(inter1 + n, _mm256_mul_ps(mvecw, msrc1));
				_mm256_store_ps(inter2 + n, _mm256_mul_ps(mvecw, msrc2));
				_mm256_store_ps(mask_ptr + n, _mm256_cmp_ps(mvecw, _mm256_setzero_ps(), 4));//4: NEQ
			}

			//blur
			GF->filter(vecW[k], vecW[k], sigma_space, spatial_order);
			GF->filter(split_inter[0], split_inter[0], sigma_space, spatial_order);
			GF->filter(split_inter[1], split_inter[1], sigma_space, spatial_order);
			GF->filter(split_inter[2], split_inter[2], sigma_space, spatial_order);

			//bilateralFilterLocalStatisticsPriorInternal(vsrc, split_inter, vecW[k], sigma_range, sigma_space, delta, blendLSPMask, BFLSPSchedule::LUTSQRT, &lut_bflsp[0]);
			bilateralFilterLocalStatisticsPriorInternal(vsrc, split_inter, vecW[k], (float)sigma_range, (float)sigma_space, delta, blendLSPMask, BFLSPSchedule::Compute);
		}
		else
		{
			if (channels == 1)
			{
				float* src0 = nullptr;
				src0 = vsrc[0].ptr<float>();//for gray and color


				float* inter0 = nullptr;
				inter0 = split_inter[0].ptr<float>();//for gray and color

				for (int n = 0; n < imsize; n += 8)
				{
					__m256 mvecw = _mm256_load_ps(vecw_ptr + n);
					_mm256_store_ps(inter0 + n, _mm256_mul_ps(mvecw, _mm256_load_ps(src0 + n)));
				}

				GF->filter(vecW[k], vecW[k], sigma_space, spatial_order);
				GF->filter(split_inter[0], split_inter[0], sigma_space, spatial_order);
			}
			else if (channels == 3)
			{
				__m256* idx = (__m256*)index.ptr<float>();
				__m256* vecw_ptr = (__m256*)vecW[k].ptr<float>();
				__m256* src0 = (downSampleImage == 1) ? (__m256*)vsrc[0].ptr<float>() : (__m256*)vsrcRes[0].ptr<float>();
				__m256* src1 = (downSampleImage == 1) ? (__m256*)vsrc[1].ptr<float>() : (__m256*)vsrcRes[1].ptr<float>();
				__m256* src2 = (downSampleImage == 1) ? (__m256*)vsrc[2].ptr<float>() : (__m256*)vsrcRes[2].ptr<float>();
				__m256* inter0 = (__m256*)split_inter[0].ptr<float>();
				__m256* inter1 = (__m256*)split_inter[1].ptr<float>();
				__m256* inter2 = (__m256*)split_inter[2].ptr<float>();

				isWRedunductLoadDecomposition = false;
				if (isWRedunductLoadDecomposition)
				{
					for (int n = 0; n < IMSIZE8; n++)
					{
						const __m256 mvecw = *vecw_ptr++;
						*inter0++ = _mm256_mul_ps(mvecw, *src0++);
					}
					GF->filter(split_inter[0], split_inter[0], sigma_space / downSampleImage, spatial_order);

					vecw_ptr = (__m256*)vecW[k].ptr<float>();
					for (int n = 0; n < IMSIZE8; n++)
					{
						const __m256 mvecw = *vecw_ptr++;
						*inter1++ = _mm256_mul_ps(mvecw, *src1++);
					}
					GF->filter(split_inter[1], split_inter[1], sigma_space / downSampleImage, spatial_order);

					vecw_ptr = (__m256*)vecW[k].ptr<float>();
					for (int n = 0; n < IMSIZE8; n++)
					{
						const __m256 mvecw = *vecw_ptr++;
						*inter2++ = _mm256_mul_ps(mvecw, *src2++);
					}
					GF->filter(split_inter[2], split_inter[2], sigma_space / downSampleImage, spatial_order);
					GF->filter(vecW[k], vecW[k], sigma_space, spatial_order);
				}
				else
				{
					for (int n = 0; n < IMSIZE8; n++)
					{
						const __m256 mvecw = *vecw_ptr++;
						//const __m256 mvecw = _mm256_andnot_ps(_mm256_set1_ps(1.f), _mm256_cmp_ps(*idx++, _mm256_set1_ps(float(k)), 0));
						//*vecw_ptr++ = mvecw;
						*inter0++ = _mm256_mul_ps(mvecw, *src0++);
						*inter1++ = _mm256_mul_ps(mvecw, *src1++);
						*inter2++ = _mm256_mul_ps(mvecw, *src2++);
					}
					GF->filter(vecW[k], vecW[k], sigma_space / downSampleImage, spatial_order);
					GF->filter(split_inter[0], split_inter[0], sigma_space / downSampleImage, spatial_order);
					GF->filter(split_inter[1], split_inter[1], sigma_space / downSampleImage, spatial_order);
					GF->filter(split_inter[2], split_inter[2], sigma_space / downSampleImage, spatial_order);
				}
			}
			else
			{
				vector<__m256*> src(channels);
				vector<__m256*> inter(channels);
				__m256* vecw = (__m256*)vecW[k].ptr<float>();
				for (int c = 0; c < channels; c++)
				{
					src[c] = (__m256*)vsrc[c].ptr<float>();
					inter[c] = (__m256*)split_inter[c].ptr<float>();
				}

				if (isWRedunductLoadDecomposition)
				{
					for (int c = 0; c < channels; c++)
					{
						vecw = (__m256*)vecW[k].ptr<float>();
						for (int n = 0; n < img_size.area(); n += 8)
						{
							*inter[c]++ = _mm256_mul_ps(*vecw++, *src[c]++);
						}
						GF->filter(split_inter[c], split_inter[c], sigma_space, spatial_order);
					}
				}
				else
				{
					for (int n = 0; n < img_size.area(); n += 8)
					{
						const __m256 mvecw = *vecw++;
						for (int c = 0; c < channels; c++)
						{
							*inter[c]++ = _mm256_mul_ps(mvecw, *src[c]++);
						}
					}
					for (int c = 0; c < channels; c++)
					{
						GF->filter(split_inter[c], split_inter[c], sigma_space, spatial_order);
					}
				}
				GF->filter(vecW[k], vecW[k], sigma_space, spatial_order);
			}
		}

		//merge
		if (isUseLocalMu && !isJoint)
		{
			if (isUseFmath) mergeRecomputeAlphaForUsingMu<1>(vsrc, k, isInit);
			else mergeRecomputeAlphaForUsingMu<0>(vsrc, k, isInit);
		}
		else
		{
			merge(k, isInit);
		}
	}
	else
	{
		if (isUseLSP)
		{
			float* vecw_ptr = vecW[k].ptr<float>();
			float* mask_ptr = blendLSPMask.ptr<float>();

			float* src0 = nullptr;
			float* src1 = nullptr;
			float* src2 = nullptr;
			src0 = vsrc[0].ptr<float>();//for gray and color
			if (channels == 3)//for color
			{
				src1 = vsrc[1].ptr<float>();
				src2 = vsrc[2].ptr<float>();
			}

			float* inter0 = nullptr;
			float* inter1 = nullptr;
			float* inter2 = nullptr;
			inter0 = split_inter[0].ptr<float>();//for gray and color
			if (channels == 3)//for color
			{
				inter1 = split_inter[1].ptr<float>();
				inter2 = split_inter[2].ptr<float>();
			}

			//split
			for (int n = 0; n < img_size.area(); n += 8)
			{
				__m256 mvecw = _mm256_load_ps(vecw_ptr + n);
				__m256 msrc0 = _mm256_load_ps(src0 + n);
				__m256 msrc1 = _mm256_load_ps(src1 + n);
				__m256 msrc2 = _mm256_load_ps(src2 + n);

				_mm256_store_ps(inter0 + n, _mm256_mul_ps(mvecw, msrc0));
				_mm256_store_ps(inter1 + n, _mm256_mul_ps(mvecw, msrc1));
				_mm256_store_ps(inter2 + n, _mm256_mul_ps(mvecw, msrc2));
				_mm256_store_ps(mask_ptr + n, _mm256_cmp_ps(mvecw, _mm256_setzero_ps(), 4));//4: NEQ
			}

			//blur
			GF->filter(vecW[k], vecW[k], sigma_space, spatial_order, borderType);
			GF->filter(split_inter[0], split_inter[0], sigma_space, spatial_order, borderType);
			GF->filter(split_inter[1], split_inter[1], sigma_space, spatial_order, borderType);
			GF->filter(split_inter[2], split_inter[2], sigma_space, spatial_order, borderType);

			//bilateralFilterLocalStatisticsPriorInternal(vsrc, split_inter, vecW[k], sigma_range, sigma_space, delta, blendLSPMask, BFLSPSchedule::LUTSQRT, &lut_bflsp[0]);
			bilateralFilterLocalStatisticsPriorInternal(vsrc, split_inter, vecW[k], (float)sigma_range, (float)sigma_space, delta, blendLSPMask, BFLSPSchedule::Compute);
		}
		else
		{
			//std::cout<<"here"<<std::endl;
			const bool ch1 = true;
			const bool ch3 = true;
			//const bool ch1 = false;
			//const bool ch3 = false;
			const __m256 mone = _mm256_set1_ps(1.f);
			const __m256 mk = _mm256_set1_ps(float(k));
			if (channels == 1 && ch1)
			{
				__m256* idx = (__m256*)index.ptr<float>();
				__m256* vecw = (__m256*)vecW[0].ptr<float>();
				__m256* src0 = (downSampleImage == 1) ? (__m256*)vsrc[0].ptr<float>() : (__m256*)vsrcRes[0].ptr<float>();
				__m256* inter0 = (__m256*)split_inter[0].ptr<float>();
				for (int n = 0; n < IMSIZE8; n++)
				{
					const __m256 mvecw = _mm256_andnot_ps(mone, _mm256_cmp_ps(*idx++, mk, 0));
					*vecw++ = mvecw;
					*inter0++ = _mm256_mul_ps(mvecw, *src0++);
				}
				GF->filter(vecW[0], vecW[0], sigma_space / downSampleImage, spatial_order, borderType);
				GF->filter(split_inter[0], split_inter[0], sigma_space / downSampleImage, spatial_order, borderType);
			}
			else if (channels == 3 && ch3)
			{
				__m256* idx = (__m256*)index.ptr<float>();
				__m256* vecw = (__m256*)vecW[0].ptr<float>();
				const __m256* src0 = (downSampleImage == 1) ? (__m256*)vsrc[0].ptr<float>() : (__m256*)vsrcRes[0].ptr<float>();
				const __m256* src1 = (downSampleImage == 1) ? (__m256*)vsrc[1].ptr<float>() : (__m256*)vsrcRes[1].ptr<float>();
				const __m256* src2 = (downSampleImage == 1) ? (__m256*)vsrc[2].ptr<float>() : (__m256*)vsrcRes[2].ptr<float>();
				__m256* inter0 = (__m256*)split_inter[0].ptr<float>();
				__m256* inter1 = (__m256*)split_inter[1].ptr<float>();
				__m256* inter2 = (__m256*)split_inter[2].ptr<float>();

				//isWRedunductLoadDecomposition = false;
				if (isWRedunductLoadDecomposition)
				{
					for (int n = 0; n < IMSIZE8; n++)
					{
						const __m256 mvecw = _mm256_andnot_ps(mone, _mm256_cmp_ps(*idx++, mk, 0));
						*vecw++ = mvecw;
						*inter0++ = _mm256_mul_ps(mvecw, *src0++);
					}
					GF->filter(split_inter[0], split_inter[0], sigma_space / downSampleImage, spatial_order, borderType);

					vecw = (__m256*)vecW[0].ptr<float>();
					for (int n = 0; n < IMSIZE8; n++)
					{
						*inter1++ = _mm256_mul_ps(*vecw++, *src1++);
					}
					GF->filter(split_inter[1], split_inter[1], sigma_space / downSampleImage, spatial_order, borderType);

					vecw = (__m256*)vecW[0].ptr<float>();
					for (int n = 0; n < IMSIZE8; n++)
					{
						*inter2++ = _mm256_mul_ps(*vecw++, *src2++);
					}
					GF->filter(split_inter[2], split_inter[2], sigma_space / downSampleImage, spatial_order, borderType);
					GF->filter(vecW[0], vecW[0], sigma_space / downSampleImage, spatial_order, borderType);
				}
				else
				{
					for (int n = 0; n < IMSIZE8; n++)
					{
						const __m256 mvecw = _mm256_andnot_ps(mone, _mm256_cmp_ps(*idx++, mk, 0));
						*vecw++ = mvecw;
						*inter0++ = _mm256_mul_ps(mvecw, *src0++);
						*inter1++ = _mm256_mul_ps(mvecw, *src1++);
						*inter2++ = _mm256_mul_ps(mvecw, *src2++);
					}
					GF->filter(vecW[0], vecW[0], sigma_space / downSampleImage, spatial_order, borderType);
					GF->filter(split_inter[0], split_inter[0], sigma_space / downSampleImage, spatial_order, borderType);
					GF->filter(split_inter[1], split_inter[1], sigma_space / downSampleImage, spatial_order, borderType);
					GF->filter(split_inter[2], split_inter[2], sigma_space / downSampleImage, spatial_order, borderType);
				}
			}
			else
			{
				__m256* idx = (__m256*)index.ptr<float>();
				__m256* vecw = (__m256*)vecW[0].ptr<float>();
				vector<__m256*> src(channels);
				vector<__m256*> inter(channels);
				for (int c = 0; c < channels; c++)
				{
					src[c] = (downSampleImage == 1) ? (__m256*)vsrc[c].ptr<float>() : (__m256*)vsrcRes[c].ptr<float>();
					inter[c] = (__m256*)split_inter[c].ptr<float>();
				}

				//isWRedunductLoadDecomposition = false;
				if (isWRedunductLoadDecomposition)
				{
					//c=0
					{
						for (int n = 0; n < IMSIZE8; n++)
						{
							const __m256 mvecw = _mm256_andnot_ps(mone, _mm256_cmp_ps(*idx++, mk, 0));
							*vecw++ = mvecw;
							*inter[0]++ = _mm256_mul_ps(mvecw, *src[0]++);
						}
						GF->filter(split_inter[0], split_inter[0], sigma_space / downSampleImage, spatial_order, borderType);
					}
					for (int c = 1; c < channels; c++)
					{
						vecw = (__m256*)vecW[0].ptr<float>();
						for (int n = 0; n < IMSIZE8; n++)
						{
							*inter[c]++ = _mm256_mul_ps(*vecw++, *src[c]++);
						}
						GF->filter(split_inter[c], split_inter[c], sigma_space / downSampleImage, spatial_order, borderType);
					}
					GF->filter(vecW[0], vecW[0], sigma_space / downSampleImage, spatial_order, borderType);
				}
				else
				{
					for (int n = 0; n < IMSIZE8; n++)
					{
						const __m256 mvecw = _mm256_andnot_ps(mone, _mm256_cmp_ps(*idx++, mk, 0));
						*vecw++ = mvecw;
						for (int c = 0; c < channels; c++)
						{
							*inter[c]++ = _mm256_mul_ps(mvecw, *src[c]++);
						}
					}
					for (int c = 0; c < channels; c++)
					{
						GF->filter(split_inter[c], split_inter[c], sigma_space / downSampleImage, spatial_order, borderType);
					}
					GF->filter(vecW[0], vecW[0], sigma_space / downSampleImage, spatial_order, borderType);
				}
			}
		}

		//merge
		if (isUseLocalMu && (!isJoint))
		{
			if (isUseFmath) mergeRecomputeAlphaForUsingMu<1>(vsrc, k, isInit);
			else mergeRecomputeAlphaForUsingMu<0>(vsrc, k, isInit);
		}
		else if (isUseLocalMu && (statePCA == 1))
		{
			//std::cout << "here" << std::endl;
			if (channels == 1 && guide_channels == 1) mergeRecomputeAlphaForUsingMuPCA<1, 1, 1>(vguide, k, isInit);
			else if (channels == 1 && guide_channels == 2) mergeRecomputeAlphaForUsingMuPCA<1, 1, 2>(vguide, k, isInit);
			else if (channels == 1 && guide_channels == 3) mergeRecomputeAlphaForUsingMuPCA<1, 1, 3>(vguide, k, isInit);
			else if (channels == 3 && guide_channels == 1) mergeRecomputeAlphaForUsingMuPCA<1, 3, 1>(vguide, k, isInit);
			else if (channels == 3 && guide_channels == 2) mergeRecomputeAlphaForUsingMuPCA<1, 3, 2>(vguide, k, isInit);
			else if (channels == 3 && guide_channels == 3) mergeRecomputeAlphaForUsingMuPCA<1, 3, 3>(vguide, k, isInit);
			else if (channels == 33 && guide_channels == 1) mergeRecomputeAlphaForUsingMuPCA<1, 33, 1>(vguide, k, isInit);
			else if (channels == 33 && guide_channels == 2) mergeRecomputeAlphaForUsingMuPCA<1, 33, 2>(vguide, k, isInit);
			else if (channels == 33 && guide_channels == 3) mergeRecomputeAlphaForUsingMuPCA<1, 33, 3>(vguide, k, isInit);
			else
			{
				if (isUseFmath) mergeRecomputeAlphaForUsingMuPCA<1>(vguide, k, isInit);
				else mergeRecomputeAlphaForUsingMuPCA<0>(vguide, k, isInit);
			}
		}
		else
		{
			//std::cout << "here" << std::endl;
			computeAlpha<1>((isJoint) ? vguide : vsrc, k);//K*imsize
			merge(k, isInit);
		}
	}
}

template<int channels>
void ConstantTimeHDGF_InterpolationSingle::split_blur_merge(const int k, const bool isInit, const bool isUseFmath, const bool isUseLSP)
{
	const Size size = img_size / downSampleImage;
	const int imsize = size.area();
	const int IMSIZE8 = imsize / 8;

	if (isUsePrecomputedWforeachK)
	{
		float* vecw_ptr = vecW[k].ptr<float>();

		if (isUseLSP)
		{
			float* mask_ptr = blendLSPMask.ptr<float>();

			float* src0 = nullptr;
			float* src1 = nullptr;
			float* src2 = nullptr;
			src0 = vsrc[0].ptr<float>();//for gray and color
			if (channels == 3)//for color
			{
				src1 = vsrc[1].ptr<float>();
				src2 = vsrc[2].ptr<float>();
			}

			float* inter0 = nullptr;
			float* inter1 = nullptr;
			float* inter2 = nullptr;
			inter0 = split_inter[0].ptr<float>();//for gray and color
			if (channels == 3)//for color
			{
				inter1 = split_inter[1].ptr<float>();
				inter2 = split_inter[2].ptr<float>();
			}

			//split
			for (int n = 0; n < img_size.area(); n += 8)
			{
				__m256 mvecw = _mm256_load_ps(vecw_ptr + n);
				__m256 msrc0 = _mm256_load_ps(src0 + n);
				__m256 msrc1 = _mm256_load_ps(src1 + n);
				__m256 msrc2 = _mm256_load_ps(src2 + n);

				_mm256_store_ps(inter0 + n, _mm256_mul_ps(mvecw, msrc0));
				_mm256_store_ps(inter1 + n, _mm256_mul_ps(mvecw, msrc1));
				_mm256_store_ps(inter2 + n, _mm256_mul_ps(mvecw, msrc2));
				_mm256_store_ps(mask_ptr + n, _mm256_cmp_ps(mvecw, _mm256_setzero_ps(), 4));//4: NEQ
			}

			//blur
			GF->filter(vecW[k], vecW[k], sigma_space, spatial_order);
			GF->filter(split_inter[0], split_inter[0], sigma_space, spatial_order);
			GF->filter(split_inter[1], split_inter[1], sigma_space, spatial_order);
			GF->filter(split_inter[2], split_inter[2], sigma_space, spatial_order);

			//bilateralFilterLocalStatisticsPriorInternal(vsrc, split_inter, vecW[k], sigma_range, sigma_space, delta, blendLSPMask, BFLSPSchedule::LUTSQRT, &lut_bflsp[0]);
			bilateralFilterLocalStatisticsPriorInternal(vsrc, split_inter, vecW[k], (float)sigma_range, (float)sigma_space, delta, blendLSPMask, BFLSPSchedule::Compute);
		}
		else
		{
			if (channels == 1)
			{
				float* src0 = nullptr;
				src0 = vsrc[0].ptr<float>();//for gray and color


				float* inter0 = nullptr;
				inter0 = split_inter[0].ptr<float>();//for gray and color

				for (int n = 0; n < imsize; n += 8)
				{
					__m256 mvecw = _mm256_load_ps(vecw_ptr + n);
					_mm256_store_ps(inter0 + n, _mm256_mul_ps(mvecw, _mm256_load_ps(src0 + n)));
				}

				GF->filter(vecW[k], vecW[k], sigma_space, spatial_order);
				GF->filter(split_inter[0], split_inter[0], sigma_space, spatial_order);
			}
			else if (channels == 3)
			{
				__m256* idx = (__m256*)index.ptr<float>();
				__m256* vecw_ptr = (__m256*)vecW[k].ptr<float>();
				__m256* src0 = (downSampleImage == 1) ? (__m256*)vsrc[0].ptr<float>() : (__m256*)vsrcRes[0].ptr<float>();
				__m256* src1 = (downSampleImage == 1) ? (__m256*)vsrc[1].ptr<float>() : (__m256*)vsrcRes[1].ptr<float>();
				__m256* src2 = (downSampleImage == 1) ? (__m256*)vsrc[2].ptr<float>() : (__m256*)vsrcRes[2].ptr<float>();
				__m256* inter0 = (__m256*)split_inter[0].ptr<float>();
				__m256* inter1 = (__m256*)split_inter[1].ptr<float>();
				__m256* inter2 = (__m256*)split_inter[2].ptr<float>();

				isWRedunductLoadDecomposition = false;
				if (isWRedunductLoadDecomposition)
				{
					for (int n = 0; n < IMSIZE8; n++)
					{
						const __m256 mvecw = *vecw_ptr++;
						*inter0++ = _mm256_mul_ps(mvecw, *src0++);
					}
					GF->filter(split_inter[0], split_inter[0], sigma_space / downSampleImage, spatial_order);

					vecw_ptr = (__m256*)vecW[k].ptr<float>();
					for (int n = 0; n < IMSIZE8; n++)
					{
						const __m256 mvecw = *vecw_ptr++;
						*inter1++ = _mm256_mul_ps(mvecw, *src1++);
					}
					GF->filter(split_inter[1], split_inter[1], sigma_space / downSampleImage, spatial_order);

					vecw_ptr = (__m256*)vecW[k].ptr<float>();
					for (int n = 0; n < IMSIZE8; n++)
					{
						const __m256 mvecw = *vecw_ptr++;
						*inter2++ = _mm256_mul_ps(mvecw, *src2++);
					}
					GF->filter(split_inter[2], split_inter[2], sigma_space / downSampleImage, spatial_order);
					GF->filter(vecW[k], vecW[k], sigma_space, spatial_order);
				}
				else
				{
					for (int n = 0; n < IMSIZE8; n++)
					{
						const __m256 mvecw = *vecw_ptr++;
						//const __m256 mvecw = _mm256_andnot_ps(_mm256_set1_ps(1.f), _mm256_cmp_ps(*idx++, _mm256_set1_ps(float(k)), 0));
						//*vecw_ptr++ = mvecw;
						*inter0++ = _mm256_mul_ps(mvecw, *src0++);
						*inter1++ = _mm256_mul_ps(mvecw, *src1++);
						*inter2++ = _mm256_mul_ps(mvecw, *src2++);
					}
					GF->filter(vecW[k], vecW[k], sigma_space / downSampleImage, spatial_order);
					GF->filter(split_inter[0], split_inter[0], sigma_space / downSampleImage, spatial_order);
					GF->filter(split_inter[1], split_inter[1], sigma_space / downSampleImage, spatial_order);
					GF->filter(split_inter[2], split_inter[2], sigma_space / downSampleImage, spatial_order);
				}
			}
			else
			{
				vector<__m256*> src(channels);
				vector<__m256*> inter(channels);
				__m256* vecw = (__m256*)vecW[k].ptr<float>();
				for (int c = 0; c < channels; c++)
				{
					src[c] = (__m256*)vsrc[c].ptr<float>();
					inter[c] = (__m256*)split_inter[c].ptr<float>();
				}

				if (isWRedunductLoadDecomposition)
				{
					for (int c = 0; c < channels; c++)
					{
						vecw = (__m256*)vecW[k].ptr<float>();
						for (int n = 0; n < img_size.area(); n += 8)
						{
							*inter[c]++ = _mm256_mul_ps(*vecw++, *src[c]++);
						}
						GF->filter(split_inter[c], split_inter[c], sigma_space, spatial_order);
					}
				}
				else
				{
					for (int n = 0; n < img_size.area(); n += 8)
					{
						const __m256 mvecw = *vecw++;
						for (int c = 0; c < channels; c++)
						{
							*inter[c]++ = _mm256_mul_ps(mvecw, *src[c]++);
						}
					}
					for (int c = 0; c < channels; c++)
					{
						GF->filter(split_inter[c], split_inter[c], sigma_space, spatial_order);
					}
				}
				GF->filter(vecW[k], vecW[k], sigma_space, spatial_order);
			}
		}

		//merge
		if (isUseLocalMu && !isJoint)
		{
			if (isUseFmath) mergeRecomputeAlphaForUsingMu<1>(vsrc, k, isInit);
			else mergeRecomputeAlphaForUsingMu<0>(vsrc, k, isInit);
		}
		else
		{
			merge(k, isInit);
		}
	}
	else
	{
		if (isUseLSP)
		{
			float* vecw_ptr = vecW[k].ptr<float>();
			float* mask_ptr = blendLSPMask.ptr<float>();

			float* src0 = nullptr;
			float* src1 = nullptr;
			float* src2 = nullptr;
			src0 = vsrc[0].ptr<float>();//for gray and color
			if (channels == 3)//for color
			{
				src1 = vsrc[1].ptr<float>();
				src2 = vsrc[2].ptr<float>();
			}

			float* inter0 = nullptr;
			float* inter1 = nullptr;
			float* inter2 = nullptr;
			inter0 = split_inter[0].ptr<float>();//for gray and color
			if (channels == 3)//for color
			{
				inter1 = split_inter[1].ptr<float>();
				inter2 = split_inter[2].ptr<float>();
			}

			//split
			for (int n = 0; n < img_size.area(); n += 8)
			{
				__m256 mvecw = _mm256_load_ps(vecw_ptr + n);
				__m256 msrc0 = _mm256_load_ps(src0 + n);
				__m256 msrc1 = _mm256_load_ps(src1 + n);
				__m256 msrc2 = _mm256_load_ps(src2 + n);

				_mm256_store_ps(inter0 + n, _mm256_mul_ps(mvecw, msrc0));
				_mm256_store_ps(inter1 + n, _mm256_mul_ps(mvecw, msrc1));
				_mm256_store_ps(inter2 + n, _mm256_mul_ps(mvecw, msrc2));
				_mm256_store_ps(mask_ptr + n, _mm256_cmp_ps(mvecw, _mm256_setzero_ps(), 4));//4: NEQ
			}

			//blur
			GF->filter(vecW[k], vecW[k], sigma_space, spatial_order, borderType);
			GF->filter(split_inter[0], split_inter[0], sigma_space, spatial_order, borderType);
			GF->filter(split_inter[1], split_inter[1], sigma_space, spatial_order, borderType);
			GF->filter(split_inter[2], split_inter[2], sigma_space, spatial_order, borderType);

			//bilateralFilterLocalStatisticsPriorInternal(vsrc, split_inter, vecW[k], sigma_range, sigma_space, delta, blendLSPMask, BFLSPSchedule::LUTSQRT, &lut_bflsp[0]);
			bilateralFilterLocalStatisticsPriorInternal(vsrc, split_inter, vecW[k], (float)sigma_range, (float)sigma_space, delta, blendLSPMask, BFLSPSchedule::Compute);
		}
		else
		{
			//std::cout<<"here"<<std::endl;
			const bool ch1 = true;
			const bool ch3 = true;
			//const bool ch1 = false;
			//const bool ch3 = false;
			const __m256 mone = _mm256_set1_ps(1.f);
			const __m256 mk = _mm256_set1_ps(float(k));
			if (channels == 1 && ch1)
			{
				__m256* idx = (__m256*)index.ptr<float>();
				__m256* vecw = (__m256*)vecW[0].ptr<float>();
				__m256* src0 = (downSampleImage == 1) ? (__m256*)vsrc[0].ptr<float>() : (__m256*)vsrcRes[0].ptr<float>();
				__m256* inter0 = (__m256*)split_inter[0].ptr<float>();
				for (int n = 0; n < IMSIZE8; n++)
				{
					const __m256 mvecw = _mm256_andnot_ps(mone, _mm256_cmp_ps(*idx++, mk, 0));
					*vecw++ = mvecw;
					*inter0++ = _mm256_mul_ps(mvecw, *src0++);
				}
				GF->filter(vecW[0], vecW[0], sigma_space / downSampleImage, spatial_order, borderType);
				GF->filter(split_inter[0], split_inter[0], sigma_space / downSampleImage, spatial_order, borderType);
			}
			else if (channels == 3 && ch3)
			{
				__m256* idx = (__m256*)index.ptr<float>();
				__m256* vecw = (__m256*)vecW[0].ptr<float>();
				const __m256* src0 = (downSampleImage == 1) ? (__m256*)vsrc[0].ptr<float>() : (__m256*)vsrcRes[0].ptr<float>();
				const __m256* src1 = (downSampleImage == 1) ? (__m256*)vsrc[1].ptr<float>() : (__m256*)vsrcRes[1].ptr<float>();
				const __m256* src2 = (downSampleImage == 1) ? (__m256*)vsrc[2].ptr<float>() : (__m256*)vsrcRes[2].ptr<float>();
				__m256* inter0 = (__m256*)split_inter[0].ptr<float>();
				__m256* inter1 = (__m256*)split_inter[1].ptr<float>();
				__m256* inter2 = (__m256*)split_inter[2].ptr<float>();

				//isWRedunductLoadDecomposition = false;
				if (isWRedunductLoadDecomposition)
				{
					for (int n = 0; n < IMSIZE8; n++)
					{
						const __m256 mvecw = _mm256_andnot_ps(mone, _mm256_cmp_ps(*idx++, mk, 0));
						*vecw++ = mvecw;
						*inter0++ = _mm256_mul_ps(mvecw, *src0++);
					}
					GF->filter(split_inter[0], split_inter[0], sigma_space / downSampleImage, spatial_order, borderType);

					vecw = (__m256*)vecW[0].ptr<float>();
					for (int n = 0; n < IMSIZE8; n++)
					{
						*inter1++ = _mm256_mul_ps(*vecw++, *src1++);
					}
					GF->filter(split_inter[1], split_inter[1], sigma_space / downSampleImage, spatial_order, borderType);

					vecw = (__m256*)vecW[0].ptr<float>();
					for (int n = 0; n < IMSIZE8; n++)
					{
						*inter2++ = _mm256_mul_ps(*vecw++, *src2++);
					}
					GF->filter(split_inter[2], split_inter[2], sigma_space / downSampleImage, spatial_order, borderType);
					GF->filter(vecW[0], vecW[0], sigma_space / downSampleImage, spatial_order, borderType);
				}
				else
				{
					for (int n = 0; n < IMSIZE8; n++)
					{
						const __m256 mvecw = _mm256_andnot_ps(mone, _mm256_cmp_ps(*idx++, mk, 0));
						*vecw++ = mvecw;
						*inter0++ = _mm256_mul_ps(mvecw, *src0++);
						*inter1++ = _mm256_mul_ps(mvecw, *src1++);
						*inter2++ = _mm256_mul_ps(mvecw, *src2++);
					}
					GF->filter(vecW[0], vecW[0], sigma_space / downSampleImage, spatial_order, borderType);
					GF->filter(split_inter[0], split_inter[0], sigma_space / downSampleImage, spatial_order, borderType);
					GF->filter(split_inter[1], split_inter[1], sigma_space / downSampleImage, spatial_order, borderType);
					GF->filter(split_inter[2], split_inter[2], sigma_space / downSampleImage, spatial_order, borderType);
				}
			}
			else
			{
				__m256* idx = (__m256*)index.ptr<float>();
				__m256* vecw = (__m256*)vecW[0].ptr<float>();
				vector<__m256*> src(channels);
				vector<__m256*> inter(channels);
				for (int c = 0; c < channels; c++)
				{
					src[c] = (downSampleImage == 1) ? (__m256*)vsrc[c].ptr<float>() : (__m256*)vsrcRes[c].ptr<float>();
					inter[c] = (__m256*)split_inter[c].ptr<float>();
				}

				//isWRedunductLoadDecomposition = false;
				if (isWRedunductLoadDecomposition)
				{
					//c=0
					{
						for (int n = 0; n < IMSIZE8; n++)
						{
							const __m256 mvecw = _mm256_andnot_ps(mone, _mm256_cmp_ps(*idx++, mk, 0));
							*vecw++ = mvecw;
							*inter[0]++ = _mm256_mul_ps(mvecw, *src[0]++);
						}
						GF->filter(split_inter[0], split_inter[0], sigma_space / downSampleImage, spatial_order, borderType);
					}
					for (int c = 1; c < channels; c++)
					{
						vecw = (__m256*)vecW[0].ptr<float>();
						for (int n = 0; n < IMSIZE8; n++)
						{
							*inter[c]++ = _mm256_mul_ps(*vecw++, *src[c]++);
						}
						GF->filter(split_inter[c], split_inter[c], sigma_space / downSampleImage, spatial_order, borderType);
					}
					GF->filter(vecW[0], vecW[0], sigma_space / downSampleImage, spatial_order, borderType);
				}
				else
				{
					for (int n = 0; n < IMSIZE8; n++)
					{
						const __m256 mvecw = _mm256_andnot_ps(mone, _mm256_cmp_ps(*idx++, mk, 0));
						*vecw++ = mvecw;
						for (int c = 0; c < channels; c++)
						{
							*inter[c]++ = _mm256_mul_ps(mvecw, *src[c]++);
						}
					}
					for (int c = 0; c < channels; c++)
					{
						GF->filter(split_inter[c], split_inter[c], sigma_space / downSampleImage, spatial_order, borderType);
					}
					GF->filter(vecW[0], vecW[0], sigma_space / downSampleImage, spatial_order, borderType);
				}
			}
		}

		//merge
		if (isUseLocalMu && (!isJoint))
		{
			if (isUseFmath) mergeRecomputeAlphaForUsingMu<1>(vsrc, k, isInit);
			else mergeRecomputeAlphaForUsingMu<0>(vsrc, k, isInit);
		}
		else if (isUseLocalMu && (statePCA == 1))
		{
			//std::cout << "here" << std::endl;
			if (channels == 1 && guide_channels == 1) mergeRecomputeAlphaForUsingMuPCA<1, 1, 1>(vguide, k, isInit);
			else if (channels == 1 && guide_channels == 2) mergeRecomputeAlphaForUsingMuPCA<1, 1, 2>(vguide, k, isInit);
			else if (channels == 1 && guide_channels == 3) mergeRecomputeAlphaForUsingMuPCA<1, 1, 3>(vguide, k, isInit);
			else if (channels == 3 && guide_channels == 1) mergeRecomputeAlphaForUsingMuPCA<1, 3, 1>(vguide, k, isInit);
			else if (channels == 3 && guide_channels == 2) mergeRecomputeAlphaForUsingMuPCA<1, 3, 2>(vguide, k, isInit);
			else if (channels == 3 && guide_channels == 3) mergeRecomputeAlphaForUsingMuPCA<1, 3, 3>(vguide, k, isInit);
			else if (channels == 33 && guide_channels == 1) mergeRecomputeAlphaForUsingMuPCA<1, 33, 1>(vguide, k, isInit);
			else if (channels == 33 && guide_channels == 2) mergeRecomputeAlphaForUsingMuPCA<1, 33, 2>(vguide, k, isInit);
			else if (channels == 33 && guide_channels == 3) mergeRecomputeAlphaForUsingMuPCA<1, 33, 3>(vguide, k, isInit);
			else
			{
				if (isUseFmath) mergeRecomputeAlphaForUsingMuPCA<1>(vguide, k, isInit);
				else mergeRecomputeAlphaForUsingMuPCA<0>(vguide, k, isInit);
			}
		}
		else
		{
			//std::cout << "here" << std::endl;
			computeAlpha<1>((isJoint) ? vguide : vsrc, k);//K*imsize
			merge(k, isInit);
		}
	}
}

void ConstantTimeHDGF_InterpolationSingle::normalize(cv::Mat& dst)
{
	if (channels == 1)
	{
		for (int y = boundaryLength; y < img_size.height - boundaryLength; y++)
		{
			float* numer0 = numer[0].ptr<float>(y);
			float* denom_ = denom.ptr<float>(y);
			float* dptr = dst.ptr<float>(y);
			for (int x = boundaryLength; x < img_size.width - boundaryLength; x += 8)
			{
				__m256 mnumer_0 = _mm256_load_ps(numer0 + x);
				__m256 mdenom__ = _mm256_load_ps(denom_ + x);

				__m256 mb = _mm256_div_avoidzerodiv_ps(mnumer_0, mdenom__);
				//__m256 mb = _mm256_div_ps(mnumer_0, mdenom__);
				_mm256_store_ps(dptr + x, mb);
			}
		}
	}
	else if (channels == 3)
	{
		//const __m256 m255=_mm256_set1_ps(255.f);
		const bool isROI = true;
		if (isROI)
		{
			for (int y = boundaryLength; y < img_size.height - boundaryLength; y++)
			{
				__m256* numer0 = (__m256*)numer[0].ptr<float>(y, boundaryLength);
				__m256* numer1 = (__m256*)numer[1].ptr<float>(y, boundaryLength);
				__m256* numer2 = (__m256*)numer[2].ptr<float>(y, boundaryLength);
				__m256* denom_ = (__m256*)denom.ptr<float>(y, boundaryLength);
				float* dptr = dst.ptr<float>(y);
				for (int x = boundaryLength; x < img_size.width - boundaryLength; x += 8)
				{
					const __m256 mdenom__ = *denom_++;

					__m256 mb = _mm256_div_avoidzerodiv_ps(*numer0++, mdenom__);
					__m256 mg = _mm256_div_avoidzerodiv_ps(*numer1++, mdenom__);
					__m256 mr = _mm256_div_avoidzerodiv_ps(*numer2++, mdenom__);
					//mb = _mm256_max_ps(_mm256_min_ps(mb, m255), _mm256_setzero_ps());
					//mg = _mm256_max_ps(_mm256_min_ps(mg, m255), _mm256_setzero_ps());
					//mr = _mm256_max_ps(_mm256_min_ps(mr, m255), _mm256_setzero_ps());
					//__m256 mb = _mm256_div_ps(*numer0++, mdenom__);
					//__m256 mg = _mm256_div_ps(*numer1++, mdenom__);
					//__m256 mr = _mm256_div_ps(*numer2++, mdenom__);
					_mm256_store_ps_color(dptr + 3 * x, mb, mg, mr);
				}
			}
		}
		else
		{
			const int size = img_size.area();
			__m256* numer0 = (__m256*)numer[0].ptr<float>();
			__m256* numer1 = (__m256*)numer[1].ptr<float>();
			__m256* numer2 = (__m256*)numer[2].ptr<float>();
			__m256* denom_ = (__m256*)denom.ptr<float>();
			float* dptr = dst.ptr<float>();
			for (int i = 0; i < size; i += 8)
			{
				const __m256 mdenom__ = *denom_++;
				__m256 mb = _mm256_div_avoidzerodiv_ps(*numer0++, mdenom__);
				__m256 mg = _mm256_div_avoidzerodiv_ps(*numer1++, mdenom__);
				__m256 mr = _mm256_div_avoidzerodiv_ps(*numer2++, mdenom__);
				_mm256_store_ps_color(dptr + 3 * i, mb, mg, mr);
			}
		}
	}
	else
	{
		for (int y = boundaryLength; y < img_size.height - boundaryLength; y++)
		{
			AutoBuffer<__m256*> mnumer(channels);
			for (int c = 0; c < channels; c++)
			{
				mnumer[c] = (__m256*)numer[c].ptr<float>(y, boundaryLength);
			}
			const __m256* denom_ = (const __m256*)denom.ptr<float>(y, boundaryLength);
			float* dptr = dst.ptr<float>(y);
			for (int x = boundaryLength; x < img_size.width - boundaryLength; x += 8)
			{
				const __m256 mdenom__ = *denom_++;
				for (int c = 0; c < channels; c++)
				{
					*mnumer[c] = _mm256_div_avoidzerodiv_ps(*mnumer[c], mdenom__);
					dptr[channels * (x + 0) + c] = mnumer[c]->m256_f32[0];
					dptr[channels * (x + 1) + c] = mnumer[c]->m256_f32[1];
					dptr[channels * (x + 2) + c] = mnumer[c]->m256_f32[2];
					dptr[channels * (x + 3) + c] = mnumer[c]->m256_f32[3];
					dptr[channels * (x + 4) + c] = mnumer[c]->m256_f32[4];
					dptr[channels * (x + 5) + c] = mnumer[c]->m256_f32[5];
					dptr[channels * (x + 6) + c] = mnumer[c]->m256_f32[6];
					dptr[channels * (x + 7) + c] = mnumer[c]->m256_f32[7];
					mnumer[c]++;
				}
			}
		}
	}
}


void ConstantTimeHDGF_InterpolationSingle::body(const std::vector<cv::Mat>& src, cv::Mat& dst, const std::vector<cv::Mat>& guide)
{
	{
		//cp::Timer t("alloc");
		alloc(dst);
	}

	{
		//cp::Timer t("clustering");
		clustering();
	}

	{
		downsampleImage();
	}
	{
		//cp::Timer t("compute alpha");
		vector<Mat> signal = (isJoint) ? guide : src;
		vector<Mat> signalRes = (isJoint) ? vguideRes : vsrcRes;
		if (isUsePrecomputedWforeachK)
		{
			if (isUseLocalMu && !isJoint)
			{
				//std::cout << "isUseLocalMu && !isJoint" << std::endl;
				if (isUseFmath) computeW<1>(src, vecW);//K*imsize
				else computeW<0>(src, vecW);
			}
			else
			{
				//std::cout << "computeIndex" << std::endl;
				if (isUseFmath) computeWandAlpha<1>(signal);//K*imsize
				else computeWandAlpha<0>(signal);
			}
		}
		else
		{
			//std::cout << "computeIndex" << std::endl;
			computeIndex(signal, signalRes);//K*imsize
		}
	}

	{
		//cp::Timer t("blur");
		if (isUseLocalStatisticsPrior)
		{
			const float sqrt2_sr_divpi = float((sqrt(2.0) * sigma_range) / sqrt(CV_PI));
			const float sqrt2_sr_inv = float(1.0 / (sqrt(2.0) * sigma_range));
			const float eps2 = delta * sqrt2_sr_inv;
			const float exp2 = exp(-eps2 * eps2);
			const float erf2 = erf(eps2);
			lut_bflsp.resize(4430 + 1);
			lut_bflsp[0] = 1.f;
			for (int i = 1; i <= 4430; i++)
			{
				float ii = i * 0.1f;
				//float ii = i;
				const float eps1 = (2.f * ii + delta) * sqrt2_sr_inv;
				lut_bflsp[i] = ((exp(-eps1 * eps1) - exp2) / (erf(eps1) + erf2)) * sqrt2_sr_divpi / (ii + FLT_EPSILON);
			}
		}

		switch (channels)
		{
		case 1:
		{
			for (int k = 0; k < K; k++) split_blur_merge<1>(k, k == 0, isUseFmath, isUseLocalStatisticsPrior);
			break;
		}
		case 3:
		{
			for (int k = 0; k < K; k++)	split_blur_merge<3>(k, k == 0, isUseFmath, isUseLocalStatisticsPrior);
			break;
		}
		case 33:
		{
			for (int k = 0; k < K; k++) split_blur_merge<33>(k, k == 0, isUseFmath, isUseLocalStatisticsPrior);
			break;
		}
		default:
		{
			for (int k = 0; k < K; k++) split_blur_merge(k, k == 0, isUseFmath, isUseLocalStatisticsPrior);
			break;
		}
		}
	}
	{
		//cp::Timer t("normal");
		normalize(dst);
	}
}



void ConstantTimeHDGF_InterpolationSingle::setIsUseLocalMu(const bool flag)
{
	this->isUseLocalMu = flag;
}

void ConstantTimeHDGF_InterpolationSingle::setIsUseLocalStatisticsPrior(const bool flag)
{
	this->isUseLocalStatisticsPrior = flag;
}

void ConstantTimeHDGF_InterpolationSingle::setLambdaInterpolation(const float lambda)
{
	this->lambda = lambda;
}

void ConstantTimeHDGF_InterpolationSingle::setDeltaLocalStatisticsPrior(const float delta)
{
	this->delta = delta;
}