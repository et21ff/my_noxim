/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2018 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the implementation of the switch reservation table
 */

#include "ReservationTable.h"
#include <set>

ReservationTable::ReservationTable()
{
}

void ReservationTable::setSize(const int n_outputs)
{
    this->n_outputs = n_outputs;
    rtable = new TRTEntry[this->n_outputs];

    for (int i=0;i<this->n_outputs;i++)
    {
	rtable[i].index = 0;
	rtable[i].reservations.clear();
    }
}

bool ReservationTable::isNotReserved(const int port_out)
{
    assert(port_out<n_outputs);
    return (rtable[port_out].reservations.size()==0);
}

/* For a given input, returns the set of output/vc reserved from that input.
 * An index is required for each output entry, to avoid that multiple invokations
 * with different inputs returns the same output in the same clock cycle. */
std::vector<int> ReservationTable::getReservations(const int port_in, const int vc)
{
    std::vector<int> result;

    for (int o = 0; o < n_outputs; o++)
        for(auto reservation : rtable[o].reservations)
        {
            if(reservation.input == port_in && reservation.vc == vc)
            {
                result.push_back(o);
            }
        }
    return result;
}

int ReservationTable::checkReservation(const TReservation r, const int port_out)
{
    /* Sanity Check for forbidden table status:
     * - same input/VC in a different output line */
    for (int o=0;o<n_outputs;o++)
    {
	for (vector<TReservation>::size_type i=0;i<rtable[o].reservations.size(); i++)
	{
	    // In the current implementation this should never happen
	    if (o!=port_out && rtable[o].reservations[i] == r)
	    {
		return RT_ALREADY_OTHER_OUT;
	    }
	}
    }
    
     /* On a given output entry, reservations must differ by VC
     *  Motivation: they will be interleaved cycle-by-cycle as index moves */

     int n_reservations = rtable[port_out].reservations.size();
    for (int i=0;i< n_reservations; i++)
    {
	// the reservation is already present
	if (rtable[port_out].reservations[i] == r)
	    return RT_ALREADY_SAME;

	// the same VC for that output has been reserved by another input
	if (rtable[port_out].reservations[i].input != r.input &&
	    rtable[port_out].reservations[i].vc == r.vc)
	    return RT_OUTVC_BUSY;
    }
    return RT_AVAILABLE;
}

void ReservationTable::print()
{
    for (int o=0;o<n_outputs;o++)
    {
	cout << o << ": ";
	for (vector<TReservation>::size_type i=0;i<rtable[o].reservations.size();i++)
	{
	    cout << "<" << rtable[o].reservations[i].input << "," << rtable[o].reservations[i].vc << ">, ";
	}
	cout << " | " << rtable[o].index;
	cout << endl;
    }
}


void ReservationTable::reserve(const TReservation r, const int port_out)
{
    // IMPORTANT: problem when used by Hub with more connections
    //
    // reservation of reserved/not valid ports is illegal. Correctness
    // should be assured by ReservationTable users
    assert(checkReservation(r, port_out)==RT_AVAILABLE);

    // TODO: a better policy could insert in a specific position as far a possible
    // from the current index
    rtable[port_out].reservations.push_back(r);

}

void ReservationTable::release(const TReservation r, const int port_out)
{
    assert(port_out < n_outputs);

    for (vector<TReservation>::iterator i=rtable[port_out].reservations.begin();
	    i != rtable[port_out].reservations.end(); i++)
    {
	if (*i == r)
	{
	    rtable[port_out].reservations.erase(i);
	    vector<TReservation>::size_type removed_index = i - rtable[port_out].reservations.begin();

	    if (removed_index < rtable[port_out].index)
		rtable[port_out].index--;
	    else
		if (rtable[port_out].index >= rtable[port_out].reservations.size())
		    rtable[port_out].index = 0;

	    return;
	}
    }
    assert(false); //trying to release a never made reservation  ?
}

// 重载方法：检查多个输出端口的预留状态
int ReservationTable::checkReservation(const TReservation& r, const std::vector<int>& outputs)
{
    // 步骤 1: 查找并收集 r 当前被预留的所有端口
    std::vector<int> existing_ports;
    for (int o = 0; o < n_outputs; ++o) 
    {
        for (const auto& reservation : rtable[o].reservations) 
        {
            if (reservation == r) 
            {
                existing_ports.push_back(o);
                // 优化：每个输出端口的预留列表中，r 最多出现一次，找到即可跳出内层循环
                break; 
            }
        }
    }

    // 步骤 2: 根据 existing_ports 的状态进行决策
    
    // 情况 A: 预留 r 完全不存在（全新预留）
    if (existing_ports.empty()) 
    {
        // 检查所有目标端口的 VC 资源是否被其他输入占用了
        for (int port_out : outputs) 
        {
            assert(port_out < n_outputs);
            for (const auto& res : rtable[port_out].reservations) 
            {
                // 检查 VC 冲突
                if (res.input != r.input && res.vc == r.vc) 
                {
                    return RT_OUTVC_BUSY;
                }
            }
        }
        // 所有目标端口都可用
        return RT_AVAILABLE;
    } 
    // 情况 B: 预留 r 已经存在
    else 
    {
        // 将现有端口和请求端口都转换为 set 以便进行严格、无序的比较
        std::set<int> existing_ports_set(existing_ports.begin(), existing_ports.end());
        std::set<int> requested_ports_set(outputs.begin(), outputs.end());

        // 检查两个集合是否完全相同
        if (existing_ports_set == requested_ports_set) 
        {
            // 完全匹配，这是一个幂等的重复请求
            return RT_ALREADY_SAME;
        } 
        else 
        {
            // 只要不完全匹配，无论是单播vs多播，还是多播vs不同的多播，
            // 都视为资源已被一个“其他”的配置所占用。
            // 这是最清晰、最统一的冲突定义。
            return RT_ALREADY_OTHER_OUT;
        }
    }
}

// 重载方法：原子性地预留多个输出端口
void ReservationTable::reserve(const TReservation& r, const vector<int>& outputs)
{
    assert(checkReservation(r, outputs)==RT_AVAILABLE ); // 断言检查

    std::cout << "[RT::reserve] t=" << sc_time_stamp() 
              << " input=" << r.input << " vc=" << r.vc 
              << " outputs={";
    for (size_t i = 0; i < outputs.size(); ++i) {
        if (i > 0) std::cout << ",";
        std::cout << outputs[i];
    }
    std::cout << "}" << std::endl;

    // 提交阶段：预留所有端口（直接操作，避免重复检查）
    for (vector<int>::const_iterator it = outputs.begin(); it != outputs.end(); ++it)
    {
        int port_out = *it;
        // 直接预留，避免重复的断言检查
        rtable[port_out].reservations.push_back(r);
    }
}

// 重载方法：释放多个输出端口的预留
void ReservationTable::release(const TReservation& r, const vector<int>& outputs)
{
    std::cout << "[RT::release] t=" << sc_time_stamp() 
              << " input=" << r.input << " vc=" << r.vc 
              << " outputs={";
    for (size_t i = 0; i < outputs.size(); ++i) {
        if (i > 0) std::cout << ",";
        std::cout << outputs[i];
    }
    std::cout << "}" << std::endl;
    
    for (vector<int>::const_iterator it = outputs.begin(); it != outputs.end(); ++it)
    {
        int port_out = *it;
        assert(port_out < n_outputs);

        // 调用原始的单端口release方法
        release(r, port_out);
    }

    output_mappings.erase({r.input, r.vc});
}

void ReservationTable::updateIndex()
{
    for (int o=0;o<n_outputs;o++)
    {
	if (rtable[o].reservations.size()>0)
	    rtable[o].index = (rtable[o].index+1)%(rtable[o].reservations.size());
    }
}

/**
 * @brief Resets the reservation table to a clean state.
 *
 * This function clears all reservations from all output ports and resets the
 * priority index for each port. It effectively brings the table to the same
 * state it was in immediately after being initialized by setSize().
 */
void ReservationTable::reset()
{
    // A safety check to ensure the table has been allocated.
    // If setSize() has not been called, rtable will be null.
    if (rtable == nullptr) {
        return;
    }

    // Iterate over every output port in the reservation table.
    for (int i = 0; i < n_outputs; ++i) {
        // For each output port i:
        
        // 1. Clear the vector of active reservations.
        // The .clear() method removes all elements from the std::vector.
        rtable[i].reservations.clear();

        // 2. Reset the priority index to its starting position (0).
        // This is important for fair arbitration in subsequent cycles.
        rtable[i].index = 0;
    }
}

void ReservationTable::setOutputMapping(int input, int vc,   
                                         const map<int, set<int>>& mapping) {  
    output_mappings[{input, vc}] = mapping;  
}  
  
map<int, set<int>> ReservationTable::getOutputMapping(int input, int vc) {  
    auto it = output_mappings.find({input, vc});  
    return (it != output_mappings.end()) ? it->second : map<int, set<int>>();  
}
