/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2018 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the implementation of the processing element
 */

#include "ProcessingElement.h"
#include "dbg.h"
int ProcessingElement::randInt(int min, int max)
{
    return min +
	(int) ((double) (max - min + 1) * rand() / (RAND_MAX + 1.0));
}

void ProcessingElement::pe_init() {
    // 根据 local_id 分配角色
    if (local_id == 0) {
        role = ROLE_GLB;
        current_data_size = 10000; // GLB初始有大量数据
        max_capacity = 20000;
        transfer_chunk_size = 10; // 每次向SPAD传输20个单位
        downstream_node_ids.push_back(1); // 下游是ID=1 (SPAD)
        current_downstream_target_index = 0;
        receive_chunk_size=0; // GLB不接收数据
        ended_compute_loop=false;
    } else if (local_id == 1) {
        role = ROLE_SPAD;
        current_data_size = 0;
        max_capacity = 100;
        transfer_chunk_size = 5; // 每次向ComputePE传输5个单位
        downstream_node_ids.push_back(2); // 下游是ID=2 (Compute)
        current_downstream_target_index = 0;
        receive_chunk_size=10; // SPAD期望每次接收10个单位数据
        compute_loop_target = 202;
        ended_compute_loop=false;
        
    } else if (local_id == 2) {
        role = ROLE_COMPUTE;
        is_computing = false;
        is_stalled_waiting_for_data = false;
        compute_cycles_left = 0;
        required_data_per_delta=5; // ComputePE每次需要5个单位数据来进行计算
        required_data_per_fill= 10; // ComputePE每次需要10个单位数据来进行冷启动
        current_data_size = 0; // compute pe的本地buffer
        max_capacity = 20;
        receive_chunk_size=5; // ComputePE期望每次接收5个单位数据

        compute_loop_target = 100;
        compute_loop_current = 0;
        current_stage = STAGE_FILL; // 初始阶段是FILL
    }

    // 重置后，所有PE的发送状态都应该是干净的
    transmittedAtPreviousCycle = false;
}

void ProcessingElement::update_ready_signal() {
    if (reset.read()) {
        downstream_ready_out.write(false);
        // 如果需要调试 reset 分支
            dbg(sc_time_stamp(), name(), "RESET active. Writing FALSE");
        return;
    }
    bool has_space = (max_capacity - current_data_size) >= receive_chunk_size;
    
    // 计算出将要写入的值
    bool ready_to_write = has_space;

    // 写入这个值
    if (ready_to_write) {
        // 如果想写 true, 就写入自己的 ID
        downstream_ready_out.write(local_id); 
    } else {
        // 如果想写 false, 就写入一个负值，比如 -m_id
        downstream_ready_out.write(-local_id);
    }
    // if(role==ROLE_SPAD)
    // dbg(sc_time_stamp(), name(), "Updating ready signal to %d", downstream_ready_out.read());
    
}

void ProcessingElement::rxProcess() {
    // 步骤 0: 处理Reset信号 (保持不变)
    if (reset.read()) {
        ack_rx.write(0);
        current_level_rx = 0;
        // 在reset时，也应该清理其他与接收相关的状态
        return;
    }

    // 步骤 1: 遵循Noxim的握手协议 (核心结构保持不变)
    if (req_rx.read() == 1 - current_level_rx) {
        // 读取flit，这是我们处理的输入
        Flit flit = flit_rx.read();
        // ------------------- 我们注入的新逻辑 [开始] -------------------
        if (flit.flit_type == FLIT_TYPE_HEAD) {
            is_receiving_packet = true; // 包开始了，进入“接收中”状态
            update_ready_signal();
            cout << sc_time_stamp() << ": " << name() << " RX <<< HEAD from " 
                 << flit.src_id << ", packet size: " << flit.sequence_length << endl;
        }
        // 步骤 2: 只在接收到包尾(TAIL)时，才触发我们的上层逻辑
        if (flit.flit_type == FLIT_TYPE_TAIL) {
            is_receiving_packet = false; // 包结束了，退出“接收中”状态
            update_ready_signal();
            cout << sc_time_stamp() << ": " << name() << " RX <<< TAIL from " 
                 << flit.src_id << ", packet size: " << flit.sequence_length << endl;

            // 步骤 3: 根据角色执行不同的操作
            switch (role) {
                case ROLE_SPAD: {
                    // SPAD接收到来自上游(GLB)的数据
                    current_data_size.write(receive_chunk_size+current_data_size.read()); // 假设flit.size就是我们约定的块大小

                    assert(current_data_size <= max_capacity); 
                    cout << sc_time_stamp() << ": SPAD[" << local_id << "] RX data. New size: " 
                         << current_data_size << "/" << max_capacity << endl;
                    break;
                }

                case ROLE_COMPUTE: {
                    // ComputePE接收到来自SPAD的数据
                    current_data_size.write(receive_chunk_size+current_data_size.read());
                    assert(current_data_size <= max_capacity); 

                    cout << sc_time_stamp() << ": COMPUTE[" << local_id << "] RX data. New size: " 
                         << current_data_size << "/" << max_capacity<<endl;

                    // ** 事件响应: 检查是否可以解除停机状态 **
                    int required_data_per_compute = (current_stage == STAGE_FILL) ? required_data_per_fill : required_data_per_delta;

                    if (is_stalled_waiting_for_data && current_data_size >= required_data_per_compute) {
                        is_stalled_waiting_for_data = false;
                        cout << sc_time_stamp() << ": COMPUTE[" << local_id << "] Data arrived, UNSTALLING." << endl;
                    }
                    break;
                }
                
                case ROLE_GLB: {
                    // GLB 在我们的模型中是顶级生产者，它只发送不接收。
                    // 但为了完整性，可以加个日志。
                    cout << sc_time_stamp() << ": WARNING - GLB[" << local_id << "] received a packet unexpectedly." << endl;
                    break;
                }
            }
        }
        // ------------------- 我们注入的新逻辑 [结束] -------------------


        // 步骤 4: 翻转握手信号 (核心结构保持不变)
        current_level_rx = 1 - current_level_rx;
    }

    // 步骤 5: 将新的ack level写回端口 (核心结构保持不变)
    ack_rx.write(current_level_rx);
}

void ProcessingElement::txProcess()
{

    if (reset.read()) {
        req_tx.write(0);
        current_level_tx = 0;
        transmittedAtPreviousCycle = false; // <-- 注意：原始代码这里可能有点问题，reset时也应该清空队列
        while(!packet_queue.empty()) packet_queue.pop();
        return;
    }
    // ------------------- 我们注入的新逻辑 [开始] -------------------

    // 步骤 1: 决定是否要生成新的数据包
    // 只有在队列为空时，我们才考虑生成新的包。这可以防止我们无限地产生包。
    if (packet_queue.empty()) {
        transmittedAtPreviousCycle = false; // 确保可以生成新包
        
        // 根据角色调用不同的逻辑
        switch (role) {
            case ROLE_GLB:
                run_storage_logic(); // 这个函数会判断是否该生成包，如果该，就生成并push到packet_queue
                break;
            case ROLE_SPAD:
                run_storage_logic();
                break;
            case ROLE_COMPUTE:
                run_compute_logic();
                break;
        }
    }
    // ------------------- 我们注入的新逻辑 [结束] -------------------


    // ------------------- 复用Noxim的底层发送逻辑 [开始] -------------------

    // 步骤 2: 处理与下游的握手和Flit发送 (这部分代码几乎直接从原始代码复制)
    if (ack_tx.read() == current_level_tx && !packet_queue.empty() && downstream_ready_in.read() >= 0) {
        
        // 从队首数据包中取出下一个flit
        Flit flit = nextFlit();
        
        // 我们可以在这里加一些日志来调试
        if (flit.flit_type == FLIT_TYPE_HEAD) {
             cout << sc_time_stamp() << ": " << name() << " TX >>> Flit HEAD to " << flit.dst_id << endl;
        }

        flit_tx.write(flit); // 通过端口发送flit

        // 更新握手协议的状态
        current_level_tx = 1 - current_level_tx;
        req_tx.write(current_level_tx);

        // *** 在这里更新数据大小才是正确的！***
        if (flit.flit_type == FLIT_TYPE_TAIL) {
            // 在发送完最后一个flit后，才更新数据大小
            // 注意：你需要知道这个包的原始大小是多少。
            // 假设 transfer_chunk_size 是固定的
            current_data_size.write(current_data_size.read() - transfer_chunk_size);
                if (role == ROLE_SPAD) {
            compute_loop_current++;
                    } 
                if(role == ROLE_SPAD&&compute_loop_current>=compute_loop_target) 
                {
                    ended_compute_loop = true; // 不再生成包
                    cout << sc_time_stamp() << ": SPAD[" << local_id << "] All supply tasks finished. Halting new packet generation." << endl;
                }
                    // **3. 在这里打印最终的成功日志**
            cout << sc_time_stamp() << ": " << role_to_str(role) << "[" << local_id 
         << "] SUCCESSFULLY sent packet. Loop count: " 
         << ((role == ROLE_SPAD) ? to_string(compute_loop_current) : "N/A") << endl;
        }

    } 

}
// 这是 run_compute_logic() 函数，它在 txProcess() 中被调用
void ProcessingElement::run_compute_logic() {
    // 决定本次计算需要多少数据 (基于当前stage)
    int required_data_per_compute = (current_stage == STAGE_FILL) ? required_data_per_fill : required_data_per_delta;
    
    // --- 计算过程 ---
    if (is_computing) {
        compute_cycles_left--;
        if (compute_cycles_left == 0) {
            is_computing = false;
            // ** 1. 在计算完成后，立即消耗数据 **
            current_data_size.write(current_data_size.read() - required_data_per_compute);
            
            cout << sc_time_stamp() << ": COMPUTE[" << local_id << "] Finished a " 
                 << ((current_stage == STAGE_FILL) ? "FILL" : "DELTA") << " task." << endl;

            // ** 2. 更新 Stage 和 Loop Counter **
            if (current_stage == STAGE_FILL) {
                // 如果刚刚完成的是FILL, 那么下一阶段就是DELTA
                current_stage = STAGE_DELTA;
            }

            // 增加循环计数器
            compute_loop_current++;

            // ** 3. 检查是否一个大周期完成 **
            if (compute_loop_current >= compute_loop_target) {
                cout << sc_time_stamp() << ": COMPUTE[" << local_id << "] Major loop finished. Resetting..." << endl;
                compute_loop_current = 0;   // 重置计数器
                current_stage = STAGE_FILL; // 下一个大周期的开始是FILL
            }
        }
        return;
    }

    // 状态2: 如果停机等待数据，就什么也不做
    // rxProcess会负责解除这个状态
    if (is_stalled_waiting_for_data) {
        return; 
    }

    // 状态3: 空闲状态，并且数据已就绪 (因为stall状态已被解除)
    // 检查是否可以开始新的计算
    if (current_data_size >= required_data_per_compute) {
        // 数据充足，开始计算！
        is_computing = true;
        compute_cycles_left = 10;

        cout << sc_time_stamp() << ": COMPUTE[" << local_id << "] Starting compute task. "
             << "Remaining data: " <<current_data_size.read() << endl;
    } else {
        // 数据不足，进入停机等待状态
        is_stalled_waiting_for_data = true;
        cout << sc_time_stamp() << ": COMPUTE[" << local_id << "] Not enough data, STALLING." << endl;
    }
}
std::string ProcessingElement::role_to_str(const PE_Role& role) {
    switch (role) {
        case ROLE_GLB: return "GLB";
        case ROLE_SPAD: return "SPAD";
        case ROLE_COMPUTE: return "COMPUTE";
        default: return "UNKNOWN";
    }
}
void ProcessingElement::run_storage_logic()
{
    // 检查是否有足够的数据来生产一个包
    if (current_data_size.read() >= transfer_chunk_size&& !ended_compute_loop) {
        // ---- 这是两个函数完全相同的核心逻辑 ----
        int bytes_per_flit = GlobalParams::flit_size / 8;
        const int num_flits = (transfer_chunk_size + bytes_per_flit - 1) / bytes_per_flit;

        Packet pkt;
        pkt.src_id = local_id;
        pkt.dst_id = downstream_node_ids[0]; // 假设只有一个下游
        pkt.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
        pkt.size = pkt.flit_left = num_flits;
        pkt.vc_id = randInt(0, GlobalParams::n_virtual_channels - 1);
        
        packet_queue.push(pkt);

        // 使用传入的参数来打印定制化的日志
        PE_Role next_role = static_cast<PE_Role>(role + 1);
        cout << sc_time_stamp() << ": " << role_to_str(role) << "[" << local_id 
             << "] INTENDS to push a packet to queue for " << role_to_str(next_role)<< "[" 
             << pkt.dst_id << "]." << endl;
    }

}

Flit ProcessingElement::nextFlit()
{
    Flit flit;
    Packet packet = packet_queue.front();

    flit.src_id = packet.src_id;
    flit.dst_id = packet.dst_id;
    flit.vc_id = packet.vc_id;
    flit.timestamp = packet.timestamp;
    flit.sequence_no = packet.size - packet.flit_left;
    flit.sequence_length = packet.size;
    flit.hop_no = 0;
    //  flit.payload     = DEFAULT_PAYLOAD;

    flit.hub_relay_node = NOT_VALID;

    if (packet.size == packet.flit_left)
	flit.flit_type = FLIT_TYPE_HEAD;
    else if (packet.flit_left == 1)
	flit.flit_type = FLIT_TYPE_TAIL;
    else
	flit.flit_type = FLIT_TYPE_BODY;

    packet_queue.front().flit_left--;
    if (packet_queue.front().flit_left == 0)
	packet_queue.pop();

    return flit;
}

bool ProcessingElement::canShot(Packet & packet)
{
   // assert(false);
    if(never_transmit) return false;
   
    //if(local_id!=16) return false;
    /* DEADLOCK TEST 
	double current_time = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;

	if (current_time >= 4100) 
	{
	    //if (current_time==3500)
	         //cout << name() << " IN CODA " << packet_queue.size() << endl;
	    return false;
	}
	//*/

#ifdef DEADLOCK_AVOIDANCE
    if (local_id%2==0)
	return false;
#endif
    bool shot;
    double threshold;

    double now = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;

    if (GlobalParams::traffic_distribution != TRAFFIC_TABLE_BASED) {
	if (!transmittedAtPreviousCycle)
	    threshold = GlobalParams::packet_injection_rate;
	else
	    threshold = GlobalParams::probability_of_retransmission;

	shot = (((double) rand()) / RAND_MAX < threshold);
	if (shot) {
	    if (GlobalParams::traffic_distribution == TRAFFIC_RANDOM)
		    packet = trafficRandom();
        else if (GlobalParams::traffic_distribution == TRAFFIC_TRANSPOSE1)
		    packet = trafficTranspose1();
        else if (GlobalParams::traffic_distribution == TRAFFIC_TRANSPOSE2)
    		packet = trafficTranspose2();
        else if (GlobalParams::traffic_distribution == TRAFFIC_BIT_REVERSAL)
		    packet = trafficBitReversal();
        else if (GlobalParams::traffic_distribution == TRAFFIC_SHUFFLE)
		    packet = trafficShuffle();
        else if (GlobalParams::traffic_distribution == TRAFFIC_BUTTERFLY)
		    packet = trafficButterfly();
        else if (GlobalParams::traffic_distribution == TRAFFIC_LOCAL)
		    packet = trafficLocal();
        else if (GlobalParams::traffic_distribution == TRAFFIC_ULOCAL)
		    packet = trafficULocal();
        else {
            cout << "Invalid traffic distribution: " << GlobalParams::traffic_distribution << endl;
            exit(-1);
        }
	}
    } else {			// Table based communication traffic
	if (never_transmit)
	    return false;

	bool use_pir = (transmittedAtPreviousCycle == false);
	vector < pair < int, double > > dst_prob;
	double threshold =
	    traffic_table->getCumulativePirPor(local_id, (int) now, use_pir, dst_prob);

	double prob = (double) rand() / RAND_MAX;
	shot = (prob < threshold);
	if (shot) {
	    for (unsigned int i = 0; i < dst_prob.size(); i++) {
		if (prob < dst_prob[i].second) {
                    int vc = randInt(0,GlobalParams::n_virtual_channels-1);
		    packet.make(local_id, dst_prob[i].first, vc, now, getRandomSize());
		    break;
		}
	    }
	}
    }

    return shot;
}


Packet ProcessingElement::trafficLocal()
{
    Packet p;
    p.src_id = local_id;
    double rnd = rand() / (double) RAND_MAX;

    vector<int> dst_set;

    int max_id = (GlobalParams::mesh_dim_x * GlobalParams::mesh_dim_y);

    for (int i=0;i<max_id;i++)
    {
	if (rnd<=GlobalParams::locality)
	{
	    if (local_id!=i && sameRadioHub(local_id,i))
		dst_set.push_back(i);
	}
	else
	    if (!sameRadioHub(local_id,i))
		dst_set.push_back(i);
    }


    int i_rnd = rand()%dst_set.size();

    p.dst_id = dst_set[i_rnd];
    p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
    p.size = p.flit_left = getRandomSize();
    p.vc_id = randInt(0,GlobalParams::n_virtual_channels-1);
    
    return p;
}


int ProcessingElement::findRandomDestination(int id, int hops)
{
    assert(GlobalParams::topology == TOPOLOGY_MESH);

    int inc_y = rand()%2?-1:1;
    int inc_x = rand()%2?-1:1;
    
    Coord current =  id2Coord(id);
    


    for (int h = 0; h<hops; h++)
    {

	if (current.x==0)
	    if (inc_x<0) inc_x=0;

	if (current.x== GlobalParams::mesh_dim_x-1)
	    if (inc_x>0) inc_x=0;

	if (current.y==0)
	    if (inc_y<0) inc_y=0;

	if (current.y==GlobalParams::mesh_dim_y-1)
	    if (inc_y>0) inc_y=0;

	if (rand()%2)
	    current.x +=inc_x;
	else
	    current.y +=inc_y;
    }
    return coord2Id(current);
}


int roulette()
{
    int slices = GlobalParams::mesh_dim_x + GlobalParams::mesh_dim_y -2;


    double r = rand()/(double)RAND_MAX;


    for (int i=1;i<=slices;i++)
    {
	if (r< (1-1/double(2<<i)))
	{
	    return i;
	}
    }
    assert(false);
    return 1;
}


Packet ProcessingElement::trafficULocal()
{
    Packet p;
    p.src_id = local_id;

    int target_hops = roulette();

    p.dst_id = findRandomDestination(local_id,target_hops);

    p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
    p.size = p.flit_left = getRandomSize();
    p.vc_id = randInt(0,GlobalParams::n_virtual_channels-1);

    return p;
}

Packet ProcessingElement::trafficRandom()
{
    Packet p;
    p.src_id = local_id;
    double rnd = rand() / (double) RAND_MAX;
    double range_start = 0.0;
    int max_id;

    if (GlobalParams::topology == TOPOLOGY_MESH)
	max_id = (GlobalParams::mesh_dim_x * GlobalParams::mesh_dim_y) - 1; //Mesh 
    else    // other delta topologies
	max_id = GlobalParams::n_delta_tiles-1; 

    // Random destination distribution
    do {
	p.dst_id = randInt(0, max_id);

	// check for hotspot destination
	for (size_t i = 0; i < GlobalParams::hotspots.size(); i++) {

	    if (rnd >= range_start && rnd < range_start + GlobalParams::hotspots[i].second) {
		if (local_id != GlobalParams::hotspots[i].first ) {
		    p.dst_id = GlobalParams::hotspots[i].first;
		}
		break;
	    } else
		range_start += GlobalParams::hotspots[i].second;	// try next
	}
#ifdef DEADLOCK_AVOIDANCE
	assert((GlobalParams::topology == TOPOLOGY_MESH));
	if (p.dst_id%2!=0)
	{
	    p.dst_id = (p.dst_id+1)%256;
	}
#endif

    } while (p.dst_id == p.src_id);

    p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
    p.size = p.flit_left = getRandomSize();
    p.vc_id = randInt(0,GlobalParams::n_virtual_channels-1);

    return p;
}
// TODO: for testing only
Packet ProcessingElement::trafficTest()
{
    Packet p;
    p.src_id = local_id;
    p.dst_id = 10;

    p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
    p.size = p.flit_left = getRandomSize();
    p.vc_id = randInt(0,GlobalParams::n_virtual_channels-1);

    return p;
}

Packet ProcessingElement::trafficTranspose1()
{
    assert(GlobalParams::topology == TOPOLOGY_MESH);
    Packet p;
    p.src_id = local_id;
    Coord src, dst;

    // Transpose 1 destination distribution
    src.x = id2Coord(p.src_id).x;
    src.y = id2Coord(p.src_id).y;
    dst.x = GlobalParams::mesh_dim_x - 1 - src.y;
    dst.y = GlobalParams::mesh_dim_y - 1 - src.x;
    fixRanges(src, dst);
    p.dst_id = coord2Id(dst);

    p.vc_id = randInt(0,GlobalParams::n_virtual_channels-1);
    p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
    p.size = p.flit_left = getRandomSize();

    return p;
}

Packet ProcessingElement::trafficTranspose2()
{
    assert(GlobalParams::topology == TOPOLOGY_MESH);
    Packet p;
    p.src_id = local_id;
    Coord src, dst;

    // Transpose 2 destination distribution
    src.x = id2Coord(p.src_id).x;
    src.y = id2Coord(p.src_id).y;
    dst.x = src.y;
    dst.y = src.x;
    fixRanges(src, dst);
    p.dst_id = coord2Id(dst);

    p.vc_id = randInt(0,GlobalParams::n_virtual_channels-1);
    p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
    p.size = p.flit_left = getRandomSize();

    return p;
}

void ProcessingElement::setBit(int &x, int w, int v)
{
    int mask = 1 << w;

    if (v == 1)
	x = x | mask;
    else if (v == 0)
	x = x & ~mask;
    else
	assert(false);
}

int ProcessingElement::getBit(int x, int w)
{
    return (x >> w) & 1;
}

inline double ProcessingElement::log2ceil(double x)
{
    return ceil(log(x) / log(2.0));
}

Packet ProcessingElement::trafficBitReversal()
{

    int nbits =
	(int)
	log2ceil((double)
		 (GlobalParams::mesh_dim_x *
		  GlobalParams::mesh_dim_y));
    int dnode = 0;
    for (int i = 0; i < nbits; i++)
	setBit(dnode, i, getBit(local_id, nbits - i - 1));

    Packet p;
    p.src_id = local_id;
    p.dst_id = dnode;

    p.vc_id = randInt(0,GlobalParams::n_virtual_channels-1);
    p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
    p.size = p.flit_left = getRandomSize();

    return p;
}

Packet ProcessingElement::trafficShuffle()
{

    int nbits =
	(int)
	log2ceil((double)
		 (GlobalParams::mesh_dim_x *
		  GlobalParams::mesh_dim_y));
    int dnode = 0;
    for (int i = 0; i < nbits - 1; i++)
	setBit(dnode, i + 1, getBit(local_id, i));
    setBit(dnode, 0, getBit(local_id, nbits - 1));

    Packet p;
    p.src_id = local_id;
    p.dst_id = dnode;

    p.vc_id = randInt(0,GlobalParams::n_virtual_channels-1);
    p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
    p.size = p.flit_left = getRandomSize();

    return p;
}

Packet ProcessingElement::trafficButterfly()
{

    int nbits = (int) log2ceil((double)
		 (GlobalParams::mesh_dim_x *
		  GlobalParams::mesh_dim_y));
    int dnode = 0;
    for (int i = 1; i < nbits - 1; i++)
	setBit(dnode, i, getBit(local_id, i));
    setBit(dnode, 0, getBit(local_id, nbits - 1));
    setBit(dnode, nbits - 1, getBit(local_id, 0));

    Packet p;
    p.src_id = local_id;
    p.dst_id = dnode;

    p.vc_id = randInt(0,GlobalParams::n_virtual_channels-1);
    p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
    p.size = p.flit_left = getRandomSize();

    return p;
}

void ProcessingElement::fixRanges(const Coord src,
				       Coord & dst)
{
    // Fix ranges
    if (dst.x < 0)
	dst.x = 0;
    if (dst.y < 0)
	dst.y = 0;
    if (dst.x >= GlobalParams::mesh_dim_x)
	dst.x = GlobalParams::mesh_dim_x - 1;
    if (dst.y >= GlobalParams::mesh_dim_y)
	dst.y = GlobalParams::mesh_dim_y - 1;
}

int ProcessingElement::getRandomSize()
{
    return randInt(GlobalParams::min_packet_size,
		   GlobalParams::max_packet_size);
}

unsigned int ProcessingElement::getQueueSize() const
{
    return packet_queue.size();
}

