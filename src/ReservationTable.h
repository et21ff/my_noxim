/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2018 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the declaration of the switch reservation table
 */

#ifndef __NOXIMRESERVATIONTABLE_H__
#define __NOXIMRESERVATIONTABLE_H__

#include <cassert>
#include <vector>
#include <map>

#include "DataStructs.h"
#include "Utils.h"

using namespace std;


struct TReservation
{
    int input;
    int vc;
    inline bool operator ==(const TReservation & r) const
    {
	return (r.input==input && r.vc == vc);
    }
};

typedef struct RTEntry
{
    vector<TReservation> reservations;
    vector<TReservation>::size_type index;
} TRTEntry;

class ReservationTable {
  public:

    ReservationTable();

    inline string name() const {return "ReservationTable";};

    // check if the input/vc/output is a
    int checkReservation(const TReservation r, const int port_out);

    // Connects port_in with port_out. Asserts if port_out is reserved
    void reserve(const TReservation r, const int port_out);

    // Releases port_out connection.
    // Asserts if port_out is not reserved or not valid
    void release(const TReservation r, const int port_out);

    // Check reservation for multiple output ports
    // Returns RT_AVAILABLE if all ports are available, otherwise returns the first error code encountered
    int checkReservation(const TReservation& r, const std::vector<int>& outputs);

    // Reserve multiple output ports atomically
    // Either reserves all ports successfully or reserves none (atomic operation)
    void reserve(const TReservation& r, const std::vector<int>& outputs);

    // Release reservation for multiple output ports
    void release(const TReservation& r, const std::vector<int>& outputs);

    // Returns a map of VC to list of output ports reserved by port_in
    std::vector<int> getReservations(const int port_in , const int vc);

    // update the index of the reservation having highest priority in the current cycle
    void updateIndex();

    // check whether port_out has no reservations
    bool isNotReserved(const int port_out);

    void setSize(const int n_outputs);

    void print();

    void reset();

  private:

     TRTEntry *rtable;	// reservation vector: rtable[i] gives a RTEntry containing the set of input/VC 
			// which reserved output port

     int n_outputs;
};

#endif
