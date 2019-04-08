#include <cmath>
#include <chrono>
#include <sstream>
#include <algorithm>

#include "Tools/Exception/exception.hpp"
#include "Tools/Math/utils.h"

#include "Decoder_polar_MK_SC_naive.hpp"

using namespace aff3ct;
using namespace aff3ct::module;

template <typename B, typename R>
Decoder_polar_MK_SC_naive<B,R>
::Decoder_polar_MK_SC_naive(const int& K,
                            const int& N,
                            const tools::Polar_code& code,
                            const std::vector<bool>& frozen_bits,
                            const int n_frames)
: Decoder          (K, N, n_frames, 1),
  Decoder_SIHO<B,R>(K, N, n_frames, 1),
  code(code),
  frozen_bits(frozen_bits),
  polar_tree(code.get_stages().size() +1, code.get_kernel_matrices()[0].size()),
  Ke(code.get_kernel_matrices().size()),
  idx(code.get_biggest_kernel_size()),
  u(code.get_biggest_kernel_size()),
  LLRs(code.get_kernel_matrices()[0].size()),
  bits(code.get_kernel_matrices()[0].size() -1),
  lambdas(code.get_kernel_matrices()[0].size())
{
	const std::string name = "Decoder_polar_MK_SC_naive";
	this->set_name(name);

	if (!code.is_mono_kernel())
	{
		std::stringstream message;
		message << "'code.is_mono_kernel()' has to be true.";
		throw tools::invalid_argument(__FILE__, __LINE__, __func__, message.str());
	}

	const auto base = code.get_kernel_matrices()[0].size();
	if (base < 2)
	{
		std::stringstream message;
		message << "'base' has to be bigger or equal to 2 ('base' = " << base << ").";
		throw tools::invalid_argument(__FILE__, __LINE__, __func__, message.str());
	}

	if (this->N != code.get_codeword_size())
	{
		std::stringstream message;
		message << "'N' has to be equal to 'code.get_codeword_size()' ('N' = " << N
		        << ", 'code.get_codeword_size()' = " << code.get_codeword_size() << ").";
		throw tools::invalid_argument(__FILE__, __LINE__, __func__, message.str());
	}

	if (this->N != (int)frozen_bits.size())
	{
		std::stringstream message;
		message << "'frozen_bits.size()' has to be equal to 'N' ('frozen_bits.size()' = " << frozen_bits.size()
		        << ", 'N' = " << N << ").";
		throw tools::length_error(__FILE__, __LINE__, __func__, message.str());
	}

	auto k = 0; for (auto i = 0; i < this->N; i++) if (frozen_bits[i] == 0) k++;
	if (this->K != k)
	{
		std::stringstream message;
		message << "The number of information bits in the frozen_bits is invalid ('K' = " << K << ", 'k' = "
		        << k << ").";
		throw tools::runtime_error(__FILE__, __LINE__, __func__, message.str());
	}

	this->recursive_allocate_nodes_contents(this->polar_tree.get_root(), this->N);
	this->recursive_initialize_frozen_bits(this->polar_tree.get_root(), frozen_bits);

	for (auto ke = 0; ke < (int)this->code.get_kernel_matrices().size(); ke++)
	{
		const auto kernel_size = (int)this->code.get_kernel_matrices()[ke].size();
		Ke[ke].resize(kernel_size * kernel_size);
		for (auto i = 0; i < kernel_size; i++)
			for (auto j = 0; j < kernel_size; j++)
				this->Ke[ke][i * kernel_size +j] = (B)this->code.get_kernel_matrices()[ke][j][i];
	}

	auto same_polar_kernel = [](const std::vector<std::vector<bool>>& a, const std::vector<std::vector<bool>>& b) {
		if (a.size() != b.size())
			return false;

		for (size_t i = 0; i < a.size(); i++)
		{
			if (a[i].size() != b[i].size())
				return false;
			for (size_t j = 0; j < a[i].size(); j++)
				if (a[i][j] != b[i][j])
					return false;
		}

		return true;
	};

	if (same_polar_kernel(code.get_kernel_matrices()[0], {{1,0},{1,1}})) // Arikan kernel
	{
		lambdas[0] = [](const std::vector<R> &LLRs, const std::vector<B> &bits) -> R
		{
			// return tools::f_LLR<R>(LLRs[0], LLRs[1]);
			auto sign = std::signbit(LLRs[0]) ^ std::signbit(LLRs[1]);
			auto abs0 = std::abs(LLRs[0]);
			auto abs1 = std::abs(LLRs[1]);
			auto min = std::min(abs0, abs1);

			return sign ? -min : min;
		};

		lambdas[1] = [](const std::vector<R> &LLRs, const std::vector<B> &bits) -> R
		{
			// return tools::g_LLR<B,R>(LLRs[0], LLRs[1], bits[0]);
			return ((bits[0] == 0) ? LLRs[0] : -LLRs[0]) + LLRs[1];
		};
	}
	else if (same_polar_kernel(code.get_kernel_matrices()[0], {{1,1,1},{1,0,1},{0,1,1}}))
	{
		lambdas[0] = [](const std::vector<R> &LLRs, const std::vector<B> &bits) -> R
		{
			auto sign = std::signbit(LLRs[0]) ^ std::signbit(LLRs[1]) ^ std::signbit(LLRs[2]);
			auto abs0 = std::abs(LLRs[0]);
			auto abs1 = std::abs(LLRs[1]);
			auto abs2 = std::abs(LLRs[2]);
			auto min = std::min(std::min(abs0, abs1), abs2);

			return sign ? -min : min;
		};

		lambdas[1] = [](const std::vector<R> &LLRs, const std::vector<B> &bits) -> R
		{
			auto sign = std::signbit(LLRs[1]) ^ std::signbit(LLRs[2]);
			auto abs1 = std::abs(LLRs[1]);
			auto abs2 = std::abs(LLRs[2]);
			auto min = std::min(abs1, abs2);

			auto l1_l2 = sign ? -min : min;

			return ((bits[0] == 0) ? LLRs[0] : -LLRs[0]) + l1_l2;
		};

		lambdas[2] = [](const std::vector<R> &LLRs, const std::vector<B> &bits) -> R
		{
			return (( bits[0]            == 0) ? LLRs[1] : -LLRs[1]) +
			       (((bits[0] ^ bits[1]) == 0) ? LLRs[2] : -LLRs[2]);
		};
	}
	else if (same_polar_kernel(code.get_kernel_matrices()[0], {{1,0,0},{1,1,0},{1,0,1}}))
	{
		lambdas[0] = [](const std::vector<R> &LLRs, const std::vector<B> &bits) -> R
		{
			auto sign = std::signbit(LLRs[0]) ^ std::signbit(LLRs[1]) ^ std::signbit(LLRs[2]);
			auto abs0 = std::abs(LLRs[0]);
			auto abs1 = std::abs(LLRs[1]);
			auto abs2 = std::abs(LLRs[2]);
			auto min = std::min(std::min(abs0, abs1), abs2);

			return sign ? -min : min;
		};

		lambdas[1] = [](const std::vector<R> &LLRs, const std::vector<B> &bits) -> R
		{
			auto hl0 = ((bits[0] == 0) ? LLRs[0] : -LLRs[0]);

			auto sign = std::signbit(hl0) ^ std::signbit(LLRs[2]);
			auto abs0 = std::abs(hl0);
			auto abs2 = std::abs(LLRs[2]);
			auto min = std::min(abs0, abs2);
			auto hl0_l2 = sign ? -min : min;

			return hl0_l2 + LLRs[1];
		};

		lambdas[2] = [](const std::vector<R> &LLRs, const std::vector<B> &bits) -> R
		{
			auto hl0 = (((bits[0] ^ bits[1]) == 0) ? LLRs[0] : -LLRs[0]);

			return hl0 + LLRs[2];
		};
	}
	else
	{
		std::stringstream message;
		message << "Unsupported polar kernel.";
		throw tools::runtime_error(__FILE__, __LINE__, __func__, message.str());
	}
}

template <typename B, typename R>
Decoder_polar_MK_SC_naive<B,R>
::~Decoder_polar_MK_SC_naive()
{
	this->recursive_deallocate_nodes_contents(this->polar_tree.get_root());
}

template <typename B, typename R>
void Decoder_polar_MK_SC_naive<B,R>
::notify_frozenbits_update()
{
	this->recursive_initialize_frozen_bits(this->polar_tree.get_root(), frozen_bits);
}

template <typename B, typename R>
void Decoder_polar_MK_SC_naive<B,R>
::_load(const R *Y_N)
{
	auto *contents = this->polar_tree.get_root()->get_contents();

	for (auto i = 0; i < this->N; i++)
		contents->l[i] = Y_N[i];
}

template <typename B, typename R>
void Decoder_polar_MK_SC_naive<B,R>
::_decode_siho(const R *Y_N, B *V_K, const int frame_id)
{
//	auto t_load = std::chrono::steady_clock::now(); // ----------------------------------------------------------- LOAD
	this->_load(Y_N);
//	auto d_load = std::chrono::steady_clock::now() - t_load;

//	auto t_decod = std::chrono::steady_clock::now(); // -------------------------------------------------------- DECODE
	this->recursive_decode(this->polar_tree.get_root());
//	auto d_decod = std::chrono::steady_clock::now() - t_decod;

//	auto t_store = std::chrono::steady_clock::now(); // --------------------------------------------------------- STORE
	this->_store(V_K);
//	auto d_store = std::chrono::steady_clock::now() - t_store;

//	(*this)[dec::tsk::decode_siho].update_timer(dec::tm::decode_siho::load,   d_load);
//	(*this)[dec::tsk::decode_siho].update_timer(dec::tm::decode_siho::decode, d_decod);
//	(*this)[dec::tsk::decode_siho].update_timer(dec::tm::decode_siho::store,  d_store);
}

template <typename B, typename R>
void Decoder_polar_MK_SC_naive<B,R>
::_decode_siho_cw(const R *Y_N, B *V_N, const int frame_id)
{
//	auto t_load = std::chrono::steady_clock::now(); // ----------------------------------------------------------- LOAD
	this->_load(Y_N);
//	auto d_load = std::chrono::steady_clock::now() - t_load;

//	auto t_decod = std::chrono::steady_clock::now(); // -------------------------------------------------------- DECODE
	this->recursive_decode(this->polar_tree.get_root());
//	auto d_decod = std::chrono::steady_clock::now() - t_decod;

//	auto t_store = std::chrono::steady_clock::now(); // --------------------------------------------------------- STORE
	this->_store(V_N, true);
//	auto d_store = std::chrono::steady_clock::now() - t_store;

//	(*this)[dec::tsk::decode_siho_cw].update_timer(dec::tm::decode_siho_cw::load,   d_load);
//	(*this)[dec::tsk::decode_siho_cw].update_timer(dec::tm::decode_siho_cw::decode, d_decod);
//	(*this)[dec::tsk::decode_siho_cw].update_timer(dec::tm::decode_siho_cw::store,  d_store);
}

template <typename B, typename R>
void Decoder_polar_MK_SC_naive<B,R>
::_store(B *V, bool coded) const
{
	if (!coded)
	{
		auto k = 0;
		this->recursive_store(this->polar_tree.get_root(), V, k);
	}
	else
	{
		auto *contents_root = this->polar_tree.get_root()->get_c();
		std::copy(contents_root->s.begin(), contents_root->s.begin() + this->N, V);
	}
}

template <typename B, typename R>
void Decoder_polar_MK_SC_naive<B,R>
::recursive_allocate_nodes_contents(tools::Generic_node<Contents_MK_SC<B,R>>* node_curr, const int vector_size)
{
	const int stage = node_curr->is_root() ? this->code.get_stages().size()-1 : node_curr->get_father()->get_c()->stage -1;

	node_curr->set_contents(new Contents_MK_SC<B,R>(vector_size, stage));

	for (auto c : node_curr->get_children())
		this->recursive_allocate_nodes_contents(c, vector_size / this->code.get_kernel_matrices()[0].size());
}

template <typename B, typename R>
void Decoder_polar_MK_SC_naive<B,R>
::recursive_initialize_frozen_bits(const tools::Generic_node<Contents_MK_SC<B,R>>* node_curr,
                                   const std::vector<bool>& frozen_bits)
{
	auto *contents = node_curr->get_contents();

	if (!node_curr->is_leaf()) // stop condition
	{
		for (auto c : node_curr->get_children())
			this->recursive_initialize_frozen_bits(c, frozen_bits); // recursive call
	}
	else
		contents->is_frozen_bit = frozen_bits[node_curr->get_lane_id()];
}

template <typename B, typename R>
void Decoder_polar_MK_SC_naive<B,R>
::recursive_decode(const tools::Generic_node<Contents_MK_SC<B,R>>* node_curr)
{
	if (!node_curr->is_leaf()) // stop condition
	{
		const auto kern_size = (int)node_curr->get_children().size();
		const auto size = (int)node_curr->get_c()->l.size();
		const auto sub_part = size / kern_size;

		for (auto child = 0; child < kern_size; child++)
		{
			const auto *node_child = node_curr->get_children()[child];

			for (auto i = 0; i < sub_part; i++)
			{
				for (auto l = 0; l < kern_size; l++) LLRs[l] = node_curr->get_c()->l[l * sub_part +i];
				for (auto c = 0; c < child;     c++) bits[c] = node_curr->get_children()[c]->get_c()->s[i];

				node_child->get_c()->l[i] = lambdas[child](LLRs, bits);
			}

			this->recursive_decode(node_child); // recursive call
		}

		auto encode_polar_kernel = [](const B *u, const uint32_t *idx, const B *Ke, B *x, const int size)
		{
			for (auto i = 0; i < size; i++)
			{
				const auto stride = i * size;
				auto sum = 0;
				for (auto j = 0; j < size; j++)
					sum += u[j] & Ke[stride +j];
				x[idx[i]] = sum & (B)1;
			}
		};

		// re-encode the bits (partial sums) (generalized to all kernels)
		const auto s = node_curr->get_c()->stage;
		const auto n_kernels = (int)node_curr->get_c()->s.size() / kern_size;
		for (auto k = 0; k < n_kernels; k++)
		{
			for (auto i = 0; i < kern_size; i++)
			{
				this->idx[i] = (uint32_t)(n_kernels * i +k);
				this->u[i] = node_curr->get_children()[(this->idx[i]/sub_part)]->get_c()->s[this->idx[i]%sub_part];
			}

			encode_polar_kernel(this->u.data(),
			                    this->idx.data(),
			                    this->Ke[code.get_stages()[s]].data(),
			                    node_curr->get_c()->s.data(),
			                    kern_size);
		}
	}
	else // specific leaf treatment
	{
		node_curr->get_c()->s[0] = (!node_curr->get_c()->is_frozen_bit && // if this is a frozen bit then s == 0
		                            tools::h_LLR<B,R>(node_curr->get_c()->l[0])); // apply h()
	}
}

template <typename B, typename R>
void Decoder_polar_MK_SC_naive<B,R>
::recursive_store(const tools::Generic_node<Contents_MK_SC<B,R>>* node_curr, B *V_K, int &k) const
{
	auto *contents = node_curr->get_contents();

	if (!node_curr->is_leaf()) // stop condition
		for (auto c : node_curr->get_children())
			this->recursive_store(c, V_K, k); // recursive call
	else
		if (!frozen_bits[node_curr->get_lane_id()])
			V_K[k++] = contents->s[0];
}

template <typename B, typename R>
void Decoder_polar_MK_SC_naive<B,R>
::recursive_deallocate_nodes_contents(tools::Generic_node<Contents_MK_SC<B,R>>* node_curr)
{
	if (node_curr != nullptr)
	{
		for (auto c : node_curr->get_children())
			this->recursive_deallocate_nodes_contents(c); // recursive call

		auto *contents = node_curr->get_contents();
		delete contents;
		node_curr->set_contents(nullptr);
	}
}

// ==================================================================================== explicit template instantiation
#include "Tools/types.h"
#ifdef AFF3CT_MULTI_PREC
template class aff3ct::module::Decoder_polar_MK_SC_naive<B_8,Q_8>;
template class aff3ct::module::Decoder_polar_MK_SC_naive<B_16,Q_16>;
template class aff3ct::module::Decoder_polar_MK_SC_naive<B_32,Q_32>;
template class aff3ct::module::Decoder_polar_MK_SC_naive<B_64,Q_64>;
#else
template class aff3ct::module::Decoder_polar_MK_SC_naive<B,Q>;
#endif
// ==================================================================================== explicit template instantiation
