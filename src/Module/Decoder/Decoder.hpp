/*!
 * \file
 * \brief A Decoder is an algorithm dedicated to find the initial sequence of information bits (before the noise).
 *
 * \section LICENSE
 * This file is under MIT license (https://opensource.org/licenses/MIT).
 */
#ifndef DECODER_HPP_
#define DECODER_HPP_

#include <chrono>
#include <string>
#include <vector>
#include <stdexcept>

#include "Tools/Perf/MIPP/mipp.h"

#include "Module/Module.hpp"

namespace aff3ct
{
namespace module
{
/*!
 * \class Decoder_i
 *
 * \brief A Decoder is an algorithm dedicated to find the initial sequence of information bits (before the noise).
 *
 * \tparam B: type of the bits in the Decoder.
 * \tparam R: type of the reals (floating-point or fixed-point representation) in the Decoder.
 *
 * The Decoder takes a soft input (real numbers) and return a hard output (bits).
 * Please use Decoder for inheritance (instead of Decoder_i).
 */
template <typename B = int, typename R = float>
class Decoder_i : public Module
{
private:
	const int n_dec_waves;
	const int n_inter_frame_rest;

	std::vector<mipp::vector<R>> Y_N;
	std::vector<mipp::vector<B>> V_N;

	std::chrono::nanoseconds d_load_total;
	std::chrono::nanoseconds d_decod_total;
	std::chrono::nanoseconds d_store_total;

protected:
	const int K; /*!< Number of information bits in one frame */
	const int N; /*!< Size of one frame (= number of bits in one frame) */
	const int simd_inter_frame_level; /*!< Number of frames absorbed by the SIMD instructions. */

public:
	/*!
	 * \brief Constructor.
	 *
	 * \param K:                      number of information bits in the frame.
	 * \param N:                      size of one frame.
	 * \param n_frames:               number of frames to process in the Decoder.
	 * \param simd_inter_frame_level: number of frames absorbed by the SIMD instructions.
	 * \param name:                   Decoder's name.
	 */
	Decoder_i(const int K, const int N, const int n_frames = 1, const int simd_inter_frame_level = 1,
	          std::string name = "Decoder_i")
	: Module(n_frames, name),
	  n_dec_waves((int)std::ceil((float)this->n_frames / (float)simd_inter_frame_level)),
	  n_inter_frame_rest(this->n_frames % simd_inter_frame_level),
	  Y_N(n_dec_waves, mipp::vector<R>(simd_inter_frame_level * N + mipp::nElReg<R>(), (R)0)),
	  V_N(n_dec_waves, mipp::vector<B>(simd_inter_frame_level * N + mipp::nElReg<B>(), (B)0)),
	  K(K), N(N), simd_inter_frame_level(simd_inter_frame_level)
	{
		if (K <= 0)
			throw std::invalid_argument("aff3ct::module::Decoder: \"K\" has to be greater than 0.");
		if (N <= 0)
			throw std::invalid_argument("aff3ct::module::Decoder: \"N\" has to be greater than 0.");
		if (simd_inter_frame_level <= 0)
			throw std::invalid_argument("aff3ct::module::Decoder: \"simd_inter_frame_level\" has to be greater "
			                            "than 0.");
		if (K > N)
			throw std::invalid_argument("aff3ct::module::Decoder: \"K\" has to be smaller than \"N\".");
	}

	/*!
	 * \brief Destructor.
	 */
	virtual ~Decoder_i()
	{
	}

	/*!
	 * \brief Decodes the noisy frame.
	 *
	 * \param Y_N:        a noisy frame.
	 * \param V_K:        an decoded codeword (only the information bits).
	 * \param load:       true = performs the data loading, false = do not load the data in the decoder.
	 * \param store:      true = performs the data storing, false = do not store the data in the decoder.
	 * \param store_fast: true = enables the fast data storage.
	 * \param unpack:     true = unpacks the data after the fast storage.
	 */
	void hard_decode(const mipp::vector<R>& Y_N, mipp::vector<B>& V_K,
	                 const bool load       = true,
	                 const bool store      = true,
	                 const bool store_fast = false,
	                 const bool unpack     = false)
	{
		if (this->N * this->n_frames != (int)Y_N.size())
			throw std::length_error("aff3ct::module::Decoder: \"Y_N.size()\" has to be equal to "
			                        "\"N\" * \"n_frames\".");

		if (this->N * this->n_frames < (int)V_K.size())
			throw std::length_error("aff3ct::module::Decoder: \"V_K.size()\" has to be equal or smaller than "
			                        "\"N\" * \"n_frames\".");

		using namespace std::chrono;

		this->d_load_total  = std::chrono::nanoseconds(0);
		this->d_decod_total = std::chrono::nanoseconds(0);
		this->d_store_total = std::chrono::nanoseconds(0);

		if (this->n_dec_waves == 1 && this->n_inter_frame_rest == 0)
		{
			__hard_decode(Y_N, V_K, load, store, store_fast, unpack);
		}
		else
		{
			for (auto w = 0; w < this->n_dec_waves; w++)
			{
				auto t_load = steady_clock::now();
				const int n_frames_per_wave = (w == this->n_dec_waves -1 && this->n_inter_frame_rest != 0) ?
				                              this->n_inter_frame_rest :
				                              this->simd_inter_frame_level;

				if (load)
				{
					const auto waves_off1 = w * this->simd_inter_frame_level * this->N;
					std::copy(Y_N.begin() + waves_off1,
					          Y_N.begin() + waves_off1 + n_frames_per_wave * this->N,
					          this->Y_N[w].begin());
				}
				this->d_load_total += steady_clock::now() - t_load;

				__hard_decode(this->Y_N[w], this->V_N[w], load, store, store_fast, unpack);

				auto t_store = steady_clock::now();
				if (store)
				{
					if (this->K * this->n_frames == (int)V_K.size())
					{
						const auto waves_off2 = w * this->simd_inter_frame_level * this->K;
						std::copy(this->V_N[w].begin(),
						          this->V_N[w].begin() + n_frames_per_wave * this->K,
						          V_K.begin() + waves_off2);
					}
					else if (this->N * this->n_frames == (int)V_K.size())
					{
						const auto waves_off3 = w * this->simd_inter_frame_level * this->N;
						std::copy(this->V_N[w].begin(),
						          this->V_N[w].begin() + n_frames_per_wave * this->N,
						          V_K.begin() + waves_off3);
					}
					else
						throw std::runtime_error("aff3ct::module::Decoder: this should never happen, \"V_K\" is not "
						                         "a multiple of \"K\" or of \"N\".");
				}
				this->d_store_total += steady_clock::now() - t_store;
			}
		}
	}

	/*!
	 * \brief Gets the duration of the data loading in the decoding process.
	 *
	 * \return the duration of the data loading in the decoding process.
	 */
	std::chrono::nanoseconds get_load_duration() const
	{
		return this->d_load_total;
	}

	/*!
	 * \brief Gets the duration of the decoding process (without loads and stores).
	 *
	 * \return the duration of the decoding process (without loads and stores).
	 */
	std::chrono::nanoseconds get_decode_duration() const
	{
		return this->d_decod_total;
	}

	/*!
	 * \brief Gets the duration of the data storing in the decoding process.
	 *
	 * \return the duration of the data storing in the decoding process.
	 */
	std::chrono::nanoseconds get_store_duration() const
	{
		return this->d_store_total;
	}

	/*!
	 * \brief Gets the number of frames absorbed by the SIMD instructions.
	 *
	 * \return the number of frames absorbed by the SIMD instructions.
	 */
	int get_simd_inter_frame_level() const
	{
		return this->simd_inter_frame_level;
	}

protected:
	/*!
	 * \brief Loads the noisy frame into the Decoder.
	 *
	 * \param Y_N: a noisy frame.
	 */
	virtual void _load(const mipp::vector<R>& Y_N) = 0;

	/*!
	 * \brief Decodes the noisy frame (have to be called after the load method).
	 */
	virtual void _hard_decode() = 0;

	/*!
	 * \brief Stores the decoded information bits (have to be called after the decode method).
	 *
	 * \param V_K: an decoded codeword (only the information bits).
	 */
	virtual void _store(mipp::vector<B>& V_K) const = 0;

	/*!
	 * \brief Stores the decoded codeword, may or may not contain the redundancy bits (parity) (should be called over
	 *        the standard store method).
	 *
	 * \param V: the decoded codeword.
	 */
	virtual void _store_fast(mipp::vector<B>& V) const { _store(V); }

	/*!
	 * \brief Can be called after the store_fast method if store_fast return the bits in a non-standard format. The
	 *        unpack method converts those bits into a standard format.
	 *
	 * \param V: the decoded and unpacked codeword.
	 */
	virtual void _unpack(mipp::vector<B>& V) const {}

private:
	inline void __hard_decode(const mipp::vector<R>& Y_N, mipp::vector<B>& V_K,
	                          const bool load       = true,
	                          const bool store      = true,
	                          const bool store_fast = false,
	                          const bool unpack     = false)
	{
		using namespace std::chrono;

		auto t_load = steady_clock::now();
		if (load)
			this->load(Y_N);
		this->d_load_total += steady_clock::now() - t_load;

		auto t_decod = steady_clock::now();
		this->_hard_decode();
		this->d_decod_total += steady_clock::now() - t_decod;

		auto t_store = steady_clock::now();
		if (store)
		{
			if (store_fast)
			{
				this->store_fast(V_K);
				if (unpack)
					this->unpack(V_K);
			}
			else
				this->store(V_K);
		}
		this->d_store_total += steady_clock::now() - t_store;
	}

	inline void load(const mipp::vector<R>& Y_N)
	{
		if (this->N * this->simd_inter_frame_level > (int)Y_N.size())
			throw std::length_error("aff3ct::module::Decoder: \"Y_N.size()\" has to be greater than "
			                        "\"N\" * \"simd_inter_frame_level\".");

		this->_load(Y_N);
	}

	inline void store(mipp::vector<B>& V_K) const
	{
		if (this->K * this->simd_inter_frame_level > (int)V_K.size())
			throw std::length_error("aff3ct::module::Decoder: \"V_K.size()\" has to be greater than "
			                        "\"K\" * \"simd_inter_frame_level\".");

		this->_store(V_K);
	}

	inline void store_fast(mipp::vector<B>& V) const
	{
		this->_store_fast(V);
	}

	inline void unpack(mipp::vector<B>& V) const
	{
		this->_unpack(V);
	}
};
}
}

#include "SC_Decoder.hpp"

#endif /* DECODER_HPP_ */
