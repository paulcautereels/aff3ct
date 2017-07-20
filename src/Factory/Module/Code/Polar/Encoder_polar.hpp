#ifndef FACTORY_ENCODER_POLAR_HPP
#define FACTORY_ENCODER_POLAR_HPP

#include <string>
#include <mipp.h>

#include "Module/Encoder/Encoder.hpp"

#include "../Encoder.hpp"

namespace aff3ct
{
namespace factory
{
struct Encoder_polar : public Encoder
{
	template <typename B = int>
	static module::Encoder<B>* build(const parameters      &params,
	                                 const mipp::vector<B> &frozen_bits);

	static void build_args(arg_map &req_args, arg_map &opt_args);
	static void store_args(const tools::Arguments_reader& ar, parameters &params);
	static void group_args(arg_grp& ar);

	static void header(params_list& head_enc, const parameters& params);
};
}
}

#endif /* FACTORY_ENCODER_POLAR_HPP */