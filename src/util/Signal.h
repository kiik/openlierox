#ifndef OLX_UTIL_SIGNAL_H
#define OLX_UTIL_SIGNAL_H

#include <functional>
#include <vector>
#include <cstddef>

// Minimal replacement for the small subset of boost::signals2::signal that
// OpenLieroX uses: connect a slot, invoke all slots in connection order, and
// drop every slot. Connections are not individually revocable and this is not
// thread-safe -- neither is needed here.
template<typename Signature>
class Signal;

template<typename R, typename... Args>
class Signal<R(Args...)> {
public:
	typedef std::function<R(Args...)> slot_type;

	void connect(const slot_type& slot) { m_slots.push_back(slot); }

	void operator()(Args... args) const {
		for(size_t i = 0; i < m_slots.size(); ++i)
			m_slots[i](args...);
	}

	void disconnect_all_slots() { m_slots.clear(); }

private:
	std::vector<slot_type> m_slots;
};

#endif // OLX_UTIL_SIGNAL_H
