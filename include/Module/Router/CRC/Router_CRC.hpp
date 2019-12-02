/*!
 * \file
 * \brief Class module::Router_CRC.
 */
#ifndef ROUTER_CRC_HPP_
#define ROUTER_CRC_HPP_

#include <memory>

#include "Module/CRC/CRC.hpp"
#include "Module/Router/Router.hpp"

namespace aff3ct
{
namespace module
{
template <typename IN = int, typename OUT = int>
class Router_CRC : public Router<IN,OUT>
{
	std::shared_ptr<CRC<IN>> crc;

public:
	Router_CRC(const CRC<IN> &crc, const size_t n_elmts_out);
	virtual ~Router_CRC() = default;
	virtual Router_CRC<IN,OUT>* clone() const;

protected:
	virtual void deep_copy(const Router_CRC<IN,OUT> &m);

	virtual size_t _route(const IN *in, const int frame_id);

	virtual size_t select_route_inter(const size_t a, const size_t b);
};
}
}

#ifndef DOXYGEN_SHOULD_SKIP_THIS
#include "Module/Router/CRC/Router_CRC.hxx"
#endif

#endif /* ROUTER_CRC_HPP_ */
