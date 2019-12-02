/*!
 * \file
 * \brief Class module::Router.
 */
#ifndef ROUTER_HPP_
#define ROUTER_HPP_

#include <cstddef>

#include "Module/Task.hpp"
#include "Module/Socket.hpp"
#include "Module/Module.hpp"

namespace aff3ct
{
namespace module
{
	namespace rtr
	{
		enum class tsk : size_t { route, SIZE };

		namespace sck
		{
			enum class route : size_t { in, out, out1, out2, SIZE };
		}
	}

template <typename IN, typename OUT>
class Router : public Module
{
public:
	inline Task&   operator[](const rtr::tsk        t);
	inline Socket& operator[](const rtr::sck::route s);

protected:
	const size_t n_elmts_in;
	const size_t n_elmts_out;
	const size_t n_outputs;

public:
	Router(const size_t n_elmts_in, const size_t n_elmts_out, const size_t n_outputs, const int n_frames = 1);
	virtual ~Router() = default;

	size_t get_n_elmts_in() const;

	size_t get_n_elmts_out() const;

	size_t get_n_outputs() const;

	template <class A = std::allocator<IN>>
	size_t route(const std::vector<IN,A>& in, const int frame_id = -1);

	virtual size_t route(const IN *in, const int frame_id = -1);

protected:
	virtual size_t _route(const IN *in, const int frame_id);

	virtual size_t select_route_inter(const size_t a, const size_t b);
};
}
}

#ifndef DOXYGEN_SHOULD_SKIP_THIS
#include "Module/Router/Router.hxx"
#endif

#endif /* ROUTER_HPP_ */
