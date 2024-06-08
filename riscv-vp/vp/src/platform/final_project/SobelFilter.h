#ifndef SOBEL_FILTER_H_
#define SOBEL_FILTER_H_
#include <systemc>
#include <cmath>
#include <algorithm>
#include <iomanip>
using namespace sc_core;

#include <tlm>
#include <tlm_utils/simple_target_socket.h>

#include "filter_def.h"



struct SobelFilter : public sc_module {
  tlm_utils::simple_target_socket<SobelFilter> tsock;

  sc_fifo<unsigned char> i_r;
  sc_fifo<unsigned char> i_g;
  sc_fifo<unsigned char> i_b;
  sc_fifo<int> o_result;
  int median[9];
  int sharp_buf[256][256];

  SC_HAS_PROCESS(SobelFilter);

  SobelFilter(sc_module_name n): 
    sc_module(n), 
    tsock("t_skt"), 
    base_offset(0) 
  {
    tsock.register_b_transport(this, &SobelFilter::blocking_transport);
    SC_THREAD(do_filter);
  }

  ~SobelFilter() {
	}

  double val;
  unsigned int base_offset;

  void do_filter(){
    { wait(CLOCK_PERIOD, SC_NS); }
    int counter = 0;
    int counter1 = 0;
    int m = 0,n = 0;
	  unsigned int valid = 0, x = 0, y = 0;
    while (true) {
      if(valid == 0){
          val = 0;
          //wait(CLOCK_PERIOD, SC_NS);
        for (unsigned int v = 0; v < MASK_Y; v++) {
          for (unsigned int u = 0; u < MASK_X; u++) {
            //printf("before read\n");
            double grey = (i_r.read() + i_g.read() + i_b.read()) / 3;
            median[3*v+u]=grey;
            counter1++;
            //printf("grey = %f\n",grey);
            //printf("!!!!!!!!!! read %d 次 !!!!!!!!!!\n",counter1);
            wait(CLOCK_PERIOD, SC_NS);
          }
        }
        std::sort(median, median+9);
        sharp_buf[n][m]= median[4];
        n++;
        if (n == 256) {
        //if (n == 25) {
          m++;
          n = 0;
          if (m > 255) valid = 1;
          //if (m > 24) valid = 1;
        }
      }
      if (valid == 1){
        int ans, sharp = 0;
			  for (int q = -1; q != 1 + 1; q++) {   //-1, 0, 1
				  for (int r = -1; r != 1 + 1; r++) { //-1, 0, 1
            if (x + r >= 0 && x + r < 256 && y + q >= 0 && y + q < 256) {
            //if (x + r >= 0 && x + r < 25 && y + q >= 0 && y + q < 25) {
              ans = sharp_buf[x+r][y+q];
            }
            else ans = 0;
            sharp += ans * Lap_mask[r+1][q+1];
          }
        }
        //printf("sharp = %d\n",sharp);
        x++;
        if (x == 256){
        //if (x == 25){
          y++;
          x = 0;
        }
        val = sharp;
        double total = 0;
        total = val * val;
        //wait(CLOCK_PERIOD, SC_NS);
        int result = static_cast<int>(std::sqrt(total));

        // cout << (int)result << endl;
        counter++;
        //printf("!!!!!!!!!! write %d 次 !!!!!!!!!!\n",counter);
        //printf("result = %d\n",result);
        o_result.write(result);
      }
    }
  }

int counter111 = 0;
  void blocking_transport(tlm::tlm_generic_payload &payload, sc_core::sc_time &delay){
    wait(delay);
    // unsigned char *mask_ptr = payload.get_byte_enable_ptr();
    // auto len = payload.get_data_length();
    tlm::tlm_command cmd = payload.get_command();
    sc_dt::uint64 addr = payload.get_address();
    unsigned char *data_ptr = payload.get_data_ptr();

    addr -= base_offset;


    // cout << (int)data_ptr[0] << endl;
    // cout << (int)data_ptr[1] << endl;
    // cout << (int)data_ptr[2] << endl;
    word buffer;
    int printdata;
    switch (cmd) {
      case tlm::TLM_READ_COMMAND:
        // cout << "READ" << endl;
        switch (addr) {
          case SOBEL_FILTER_RESULT_ADDR:
            printdata = data_ptr[0];
            /*
            if (counter111 <= 10){
              std::cout << "data_ptr[0] " << printdata << std::endl;
              counter111++;
            }
            */
            buffer.uint = o_result.read();
            break;
          default:
            std::cerr << "READ Error! SobelFilter::blocking_transport: address 0x"
                      << std::setfill('0') << std::setw(8) << std::hex << addr
                      << std::dec << " is not valid" << std::endl;
          }
        data_ptr[0] = buffer.uc[0];
        data_ptr[1] = buffer.uc[1];
        data_ptr[2] = buffer.uc[2];
        data_ptr[3] = buffer.uc[3];
        break;
      case tlm::TLM_WRITE_COMMAND:
        // cout << "WRITE" << endl;
        switch (addr) {
          case SOBEL_FILTER_R_ADDR:
          /*
            printdata = data_ptr[0];
            if (counter111 <= 10){
              std::cout << "data_ptr[0]" << printdata << std::endl;
              counter111++;
            }
          */  
            i_r.write(data_ptr[0]);
            i_g.write(data_ptr[1]);
            i_b.write(data_ptr[2]);
            break;
          default:
            std::cerr << "WRITE Error! SobelFilter::blocking_transport: address 0x"
                      << std::setfill('0') << std::setw(8) << std::hex << addr
                      << std::dec << " is not valid" << std::endl;
        }
        break;
      case tlm::TLM_IGNORE_COMMAND:
        payload.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
        return;
      default:
        payload.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
        return;
      }
      payload.set_response_status(tlm::TLM_OK_RESPONSE); // Always OK
  }
};
#endif
