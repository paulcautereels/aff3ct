#include <string>
#include <algorithm>

#include "Module/Router/CRC/Router_CRC.hpp"

namespace aff3ct
{
namespace module
{

template <typename IN, typename OUT>
Router_CRC<IN,OUT>
::Router_CRC(const CRC<IN> &crc, const size_t n_elmts_out)
: Router<IN,OUT>(crc.get_size() + crc.get_K(), n_elmts_out, 2, crc.get_n_frames()),
: crc(crc.clone())
{
	const std::string name = "Router_CRC";
	this->set_name(name);
}

template <typename IN, typename OUT>
Router_CRC<IN,OUT>* Router_CRC<IN,OUT>
::clone() const
{
	auto m = new Router_CRC(*this);
	m->deep_copy(*this);
	return m;
}

template <typename IN, typename OUT>
void Router_CRC<IN,OUT>
::deep_copy(const Router_CRC<IN> &m)
{
	Module::deep_copy(m);
	if (m.crc != nullptr) this->crc.reset(dynamic_cast<CRC<IN>*>(m.crc->clone()));
}

template <typename IN, typename OUT>
size_t Router_CRC<IN,OUT>
::_route(const IN *in, const int frame_id)
{
	return this->crc.check(in) ? 1 : 0;
}

template <typename IN, typename OUT>
size_t Router_CRC<IN,OUT>
::select_route_inter(const size_t a, const size_t b)
{
	return std::min(a, b);
}

}
}
