#include "Task.hpp"

#include <iostream>
#include <iomanip>

#include "Tools/Display/bash_tools.h"
#include "Tools/Display/Frame_trace/Frame_trace.hpp"

#include "Module.hpp"

using namespace aff3ct;
using namespace aff3ct::module;

Task::Task(const Module &module, const std::string name, const bool autoalloc, const bool autoexec,
           const bool stats, const bool debug)
: module(module),
  name(name),
  autoalloc(autoalloc),
  autoexec(autoexec),
  stats(stats),
  debug(debug),
  debug_limit(-1),
  debug_precision(2),
  codelet([]() -> int { throw tools::unimplemented_error(__FILE__, __LINE__, __func__); return 0; }),
  n_calls(0),
  duration_total(std::chrono::nanoseconds(0)),
  duration_min(std::chrono::nanoseconds(0)),
  duration_max(std::chrono::nanoseconds(0))
{
}

std::string Task::get_name() const
{
	return this->name;
}

void Task::set_autoalloc(const bool autoalloc)
{
	if (autoalloc != this->autoalloc)
	{
		this->autoalloc = autoalloc;

		if (!autoalloc)
		{
			this->out_buffers.clear();
			for (auto &s : socket)
				if (get_socket_type(s) == OUT)
					s.dataptr = nullptr;
		}
		else
		{
			for (auto &s : socket)
				if (get_socket_type(s) == OUT)
				{
					out_buffers.push_back(std::vector<uint8_t>(s.databytes));
					s.dataptr = out_buffers.back().data();
				}
		}
	}
}

bool Task::is_autoalloc() const
{
	return this->autoalloc;
}

void Task::set_autoexec(const bool autoexec)
{
	this->autoexec = autoexec;
}

bool Task::is_autoexec() const
{
	return this->autoexec;
}

void Task::set_stats(const bool stats)
{
	this->stats = stats;
}

bool Task::is_stats() const
{
	return this->stats;
}

void Task::set_debug(const bool debug)
{
	this->debug = debug;
}

void Task::set_debug_limit(const uint32_t limit)
{
	this->debug_limit = (int32_t)limit;
}

void Task::set_debug_precision(const uint8_t prec)
{
	this->debug_precision = prec;
}

bool Task::is_debug() const
{
	return this->debug;
}

template <typename T>
static inline void display_data(const T *data,
                                const size_t fra_size, const size_t n_fra, const size_t limit,
                                const uint8_t p, const uint8_t n_spaces)
{
	if (n_fra == 1)
	{
		for (auto i = 0; i < (int)limit; i++)
			std::cout << std::fixed << std::setprecision(p) << std::setw(p +3) << +data[i]
			          << (i < (int)limit -1 ? ", " : "");
		std::cout << (limit < fra_size ? ", ..." : "");
	}
	else
	{
		const auto sty_fra = tools::Style::BOLD | tools::FG::Color::GRAY;
		std::string spaces = "#"; for (auto s = 0; s < (int)n_spaces -1; s++) spaces += " ";
		for (auto f = 0; f < (int)n_fra; f++)
		{
			std::string fra_id = tools::format("f" + std::to_string(f+1) + ":", sty_fra);
			std::cout << (f >= 1 ? spaces : "") << fra_id << "(";
			for (auto i = 0; i < (int)limit; i++)
				std::cout << std::fixed << std::setprecision(p) << std::setw(p +3) << +data[f * fra_size +i]
				          << (i < (int)limit -1 ? ", " : "");
			std::cout << (limit < fra_size ? ", ..." : "") << ")" << (f < (int)n_fra -1 ? ", \n" : "");
		}
	}
}

int Task::exec()
{
	if (can_exec())
	{
		size_t max_n_chars = 0;
		if (debug)
		{
			const auto sty_type   = tools::Style::BOLD | tools::FG::Color::MAGENTA | tools::FG::INTENSE;
			const auto sty_class  = tools::Style::BOLD;
			const auto sty_method = tools::Style::BOLD | tools::FG::Color::GREEN;

			auto n_fra = (size_t)this->module.get_n_frames();

			std::cout << "# ";
			std::cout << tools::format(module.get_name(), sty_class) << "::" << tools::format(get_name(), sty_method)
			          << "(";
			for (auto i = 0; i < (int)socket.size(); i++)
			{
				auto &s = socket[i];
				auto s_type = get_socket_type(s);
				auto n_elmts = s.get_databytes() / (size_t)s.get_datatype_size();
				std::cout << (s_type == IN ? tools::format("const ", sty_type) : "")
				          << tools::format(s.get_datatype_string(), sty_type)
				          << " " << s.get_name() << "[" << (n_fra > 1 ? std::to_string(n_fra) + "x" : "")
				          << (n_elmts / n_fra) << "]"
				          << (i < (int)socket.size() -1 ? ", " : "");

				max_n_chars = std::max(s.get_name().size(), max_n_chars);
			}
			std::cout << ")" << std::endl;

			for (auto &s : socket)
			{
				auto s_type = get_socket_type(s);
				if (s_type == IN || s_type == IN_OUT)
				{
					std::string spaces; for (size_t ss = 0; ss < max_n_chars - s.get_name().size(); ss++) spaces += " ";

					auto n_elmts = s.get_databytes() / (size_t)s.get_datatype_size();
					auto fra_size = n_elmts / n_fra;
					auto limit = debug_limit != -1 ? std::min(fra_size, (size_t)debug_limit) : fra_size;
					auto p = debug_precision;
					std::cout << "# {IN}  " << s.get_name() << spaces << " = [";
						 if (s.get_datatype() == typeid(int8_t )) display_data((int8_t *)s.get_dataptr(), fra_size, n_fra, limit, p, max_n_chars +12);
					else if (s.get_datatype() == typeid(int16_t)) display_data((int16_t*)s.get_dataptr(), fra_size, n_fra, limit, p, max_n_chars +12);
					else if (s.get_datatype() == typeid(int32_t)) display_data((int32_t*)s.get_dataptr(), fra_size, n_fra, limit, p, max_n_chars +12);
					else if (s.get_datatype() == typeid(int64_t)) display_data((int64_t*)s.get_dataptr(), fra_size, n_fra, limit, p, max_n_chars +12);
					else if (s.get_datatype() == typeid(float  )) display_data((float  *)s.get_dataptr(), fra_size, n_fra, limit, p, max_n_chars +12);
					else if (s.get_datatype() == typeid(double )) display_data((double *)s.get_dataptr(), fra_size, n_fra, limit, p, max_n_chars +12);
					std::cout << "]" << std::endl;
				}
			}
		}

		int exec_status;
		if (stats)
		{
			auto t_start = std::chrono::steady_clock::now();
			exec_status = this->codelet();
			auto duration = std::chrono::steady_clock::now() - t_start;

			this->duration_total += duration;
			if (n_calls)
			{
				this->duration_min = std::min(this->duration_min, duration);
				this->duration_max = std::max(this->duration_max, duration);
			}
			else
			{
				this->duration_min = duration;
				this->duration_max = duration;
			}
		}
		else
			exec_status = this->codelet();
		this->n_calls++;

		if (debug)
		{
			auto n_fra = (size_t)this->module.get_n_frames();
			for (auto &s : socket)
			{
				auto s_type = get_socket_type(s);
				if (s_type == OUT || s_type == IN_OUT)
				{
					std::string spaces; for (size_t ss = 0; ss < max_n_chars - s.get_name().size(); ss++) spaces += " ";

					auto n_elmts = s.get_databytes() / (size_t)s.get_datatype_size();
					auto fra_size = n_elmts / n_fra;
					auto limit = debug_limit != -1 ? std::min(fra_size, (size_t)debug_limit) : fra_size;
					auto p = debug_precision;
					std::cout << "# {OUT} " << s.get_name() << spaces << " = [";
						 if (s.get_datatype() == typeid(int8_t )) display_data((int8_t *)s.get_dataptr(), fra_size, n_fra, limit, p, max_n_chars +12);
					else if (s.get_datatype() == typeid(int16_t)) display_data((int16_t*)s.get_dataptr(), fra_size, n_fra, limit, p, max_n_chars +12);
					else if (s.get_datatype() == typeid(int32_t)) display_data((int32_t*)s.get_dataptr(), fra_size, n_fra, limit, p, max_n_chars +12);
					else if (s.get_datatype() == typeid(int64_t)) display_data((int64_t*)s.get_dataptr(), fra_size, n_fra, limit, p, max_n_chars +12);
					else if (s.get_datatype() == typeid(float  )) display_data((float  *)s.get_dataptr(), fra_size, n_fra, limit, p, max_n_chars +12);
					else if (s.get_datatype() == typeid(double )) display_data((double *)s.get_dataptr(), fra_size, n_fra, limit, p, max_n_chars +12);
					std::cout << "]" << std::endl;
				}
			}
			std::cout << "# Returned status: " << exec_status << std::endl;
			std::cout << "#" << std::endl;
		}

		return exec_status;
	}
	else
	{
		std::stringstream message;
		message << "The task cannot be executed because some of the inputs/outputs are not fed ('task.name' = "
		        << this->get_name() << ", 'module.name' = " << module.get_name() << ").";
		throw tools::runtime_error(__FILE__, __LINE__, __func__, message.str());
	}
}

template <typename T>
Socket& Task::create_socket(const std::string name, const size_t n_elmts)
{
	if (name.empty())
	{
		std::stringstream message;
		message << "Impossible to create this socket because the name is empty ('task.name' = " << this->get_name()
		        << ", 'module.name' = " << module.get_name() << ").";
		throw tools::runtime_error(__FILE__, __LINE__, __func__, message.str());
	}

	for (auto &s : socket)
		if (s.get_name() == name)
		{
			std::stringstream message;
			message << "Impossible to create this socket because an other socket has the same name ('socket.name' = "
			        << name << ", 'task.name' = " << this->get_name()
			        << ", 'module.name' = " << module.get_name() << ").";
			throw tools::runtime_error(__FILE__, __LINE__, __func__, message.str());
		}

	socket.push_back(Socket(*this, name, typeid(T), n_elmts * sizeof(T)));

	return socket.back();
}

template <typename T>
void Task::create_socket_in(const std::string name, const size_t n_elmts)
{
	auto &s = create_socket<T>(name, n_elmts);

	socket_type[s.get_name()] = Socket_type::IN;
}

template <typename T>
void Task::create_socket_in_out(const std::string name, const size_t n_elmts)
{
	auto &s = create_socket<T>(name, n_elmts);

	socket_type[s.get_name()] = Socket_type::IN_OUT;
}

template <typename T>
void Task::create_socket_out(const std::string name, const size_t n_elmts)
{
	auto &s = create_socket<T>(name, n_elmts);

	socket_type[s.get_name()] = Socket_type::OUT;

	// memory allocation
	if (is_autoalloc())
	{
		out_buffers.push_back(std::vector<uint8_t>(s.databytes));
		s.dataptr = out_buffers.back().data();
	}
}

void Task::create_codelet(std::function<int(void)> codelet)
{
	this->codelet = codelet;
}

Socket& Task::operator[](const std::string name)
{
	for (auto &s : socket)
		if (s.get_name() == name)
			return s;

	std::stringstream message;
	message << "The socket does not exist ('socket.name' = " << name << ", 'task.name' = " << this->get_name()
	        << ", 'module.name' = " << module.get_name() << ").";
	throw tools::runtime_error(__FILE__, __LINE__, __func__, message.str());
}

const Socket& Task::operator[](const std::string name) const
{
	for (auto &s : socket)
		if (s.get_name() == name)
			return s;

	std::stringstream message;
	message << "The socket does not exist ('socket.name' = " << name << ", 'task.name' = " << this->get_name()
	        << ", 'module.name' = " << module.get_name() << ").";
	throw tools::runtime_error(__FILE__, __LINE__, __func__, message.str());
}

bool Task::last_input_socket(const Socket &s_in) const
{
	const Socket* last_s_in = nullptr;
	for (auto &s : socket)
	{
		auto s_type = get_socket_type(s);
		if (s_type == IN || s_type == IN_OUT)
			last_s_in = &s;
	}

	auto val = last_s_in == nullptr ? false : &s_in == last_s_in;

	return val;
}

bool Task::can_exec() const
{
	for (auto &s : socket)
		if (s.dataptr == nullptr)
			return false;
	return true;
}

uint32_t Task::get_n_calls() const
{
	return this->n_calls;
}

std::chrono::nanoseconds Task::get_duration_total() const
{
	return this->duration_total;
}

std::chrono::nanoseconds Task::get_duration_avg() const
{
	return this->duration_total / this->n_calls;
}

std::chrono::nanoseconds Task::get_duration_min() const
{
	return this->duration_min;
}

std::chrono::nanoseconds Task::get_duration_max() const
{
	return this->duration_max;
}

const std::vector<std::string>& Task::get_registered_duration() const
{
	return this->registered_duration;
}

uint32_t Task::get_registered_n_calls(const std::string key) const
{
	return this->registered_n_calls.find(key)->second;
}

std::chrono::nanoseconds Task::get_registered_duration_total(const std::string key) const
{
	return this->registered_duration_total.find(key)->second;
}

std::chrono::nanoseconds Task::get_registered_duration_avg(const std::string key) const
{
	return this->registered_duration_total.find(key)->second / this->n_calls;
}

std::chrono::nanoseconds Task::get_registered_duration_min(const std::string key) const
{
	return this->registered_duration_min.find(key)->second;
}

std::chrono::nanoseconds Task::get_registered_duration_max(const std::string key) const
{
	return this->registered_duration_max.find(key)->second;
}

Socket_type Task::get_socket_type(const Socket &s) const
{
	const auto &it = this->socket_type.find(s.get_name());
	if (it != socket_type.end())
		return it->second;
	else
	{
		std::stringstream message;
		message << "The socket does not exist ('s.name' = " << s.name << ", 'task.name' = " << this->get_name()
		        << ", 'module.name' = " << module.get_name() << ").";
		throw tools::runtime_error(__FILE__, __LINE__, __func__, message.str());
	}
}

void Task::register_duration(const std::string key)
{
	this->registered_duration.push_back(key);
	this->registered_n_calls       [key] = 0;
	this->registered_duration_total[key] = std::chrono::nanoseconds(0);
	this->registered_duration_max  [key] = std::chrono::nanoseconds(0);
	this->registered_duration_min  [key] = std::chrono::nanoseconds(0);
}

void Task::update_duration(const std::string key, const std::chrono::nanoseconds &duration)
{
	this->registered_n_calls[key]++;
	this->registered_duration_total[key] += duration;
	if (this->n_calls)
	{
		this->registered_duration_max[key] = std::max(this->registered_duration_max[key], duration);
		this->registered_duration_min[key] = std::min(this->registered_duration_min[key], duration);
	}
	else
	{
		this->registered_duration_max[key] = duration;
		this->registered_duration_min[key] = duration;
	}
}

void Task::reset_stats()
{
	this->n_calls        =                          0;
	this->duration_total = std::chrono::nanoseconds(0);
	this->duration_min   = std::chrono::nanoseconds(0);
	this->duration_max   = std::chrono::nanoseconds(0);

	for (auto &x : this->registered_n_calls       ) x.second =                          0;
	for (auto &x : this->registered_duration_total) x.second = std::chrono::nanoseconds(0);
	for (auto &x : this->registered_duration_min  ) x.second = std::chrono::nanoseconds(0);
	for (auto &x : this->registered_duration_max  ) x.second = std::chrono::nanoseconds(0);
}

// ==================================================================================== explicit template instantiation
template void Task::create_socket_in<int8_t >(const std::string, const size_t);
template void Task::create_socket_in<int16_t>(const std::string, const size_t);
template void Task::create_socket_in<int32_t>(const std::string, const size_t);
template void Task::create_socket_in<int64_t>(const std::string, const size_t);
template void Task::create_socket_in<float  >(const std::string, const size_t);
template void Task::create_socket_in<double >(const std::string, const size_t);

template void Task::create_socket_in_out<int8_t >(const std::string, const size_t);
template void Task::create_socket_in_out<int16_t>(const std::string, const size_t);
template void Task::create_socket_in_out<int32_t>(const std::string, const size_t);
template void Task::create_socket_in_out<int64_t>(const std::string, const size_t);
template void Task::create_socket_in_out<float  >(const std::string, const size_t);
template void Task::create_socket_in_out<double >(const std::string, const size_t);

template void Task::create_socket_out<int8_t >(const std::string, const size_t);
template void Task::create_socket_out<int16_t>(const std::string, const size_t);
template void Task::create_socket_out<int32_t>(const std::string, const size_t);
template void Task::create_socket_out<int64_t>(const std::string, const size_t);
template void Task::create_socket_out<float  >(const std::string, const size_t);
template void Task::create_socket_out<double >(const std::string, const size_t);
// ==================================================================================== explicit template instantiation