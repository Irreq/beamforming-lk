
#include "pipeline.h"
#include "receiver.h"
#include "ring_buffer.h"
#include <fstream>
#include <iostream>

using namespace std;

/**
 * Connect beamformer to antenna
 */
int Pipeline::connect() {
  if (connected) {
    cerr << "Beamformer is already connected" << endl;
    return -1;
  } else if (init_receiver() == -1) {
    cerr << "Unable to establish a connection to antenna" << endl;
    return -1;
  }

  connected = 1;

  connection = thread(&Pipeline::producer, this);

  return 0;
}

/**
 * Disconnect beamformer from antenna
 */
int Pipeline::disconnect() {
  if (!connected) {
    cerr << "Beamformer is not connected" << endl;
    return -1;
  }

  {
    unique_lock<mutex> lock(pool_mutex);

    connected = 0;
  }

  connection.join();

  release_barrier();

  stop_receiving();

  return 0;
}

int Pipeline::isRunning() {
  unique_lock<mutex> lock(pool_mutex);
  return connected;
}

/**
 * check if no new data has been added
 */
int Pipeline::mostRecent() {
  unique_lock<mutex> lock(barrier_mutex);
  return modified;
}

/**
 * @brief Wait until a release is dispatched
 */
void Pipeline::barrier() {
  unique_lock<mutex> lock(barrier_mutex);
  barrier_count++;

  barrier_condition.wait(lock, [&] { return barrier_count == 0; });
}

ring_buffer &Pipeline::getRingBuffer() { return rb; }

/**
 * Allow the worker threads to continue
 */
void Pipeline::release_barrier() {
  unique_lock<mutex> lock(barrier_mutex);
  barrier_count = 0;
  modified++;
  barrier_condition.notify_all();
}

/**
 * The main distributer of data to the threads (this is also a thread)
 */
void Pipeline::producer() {
  while (isRunning()) {
    receive_offset(&rb); // Fill buffer

    {
      unique_lock<mutex> lock(barrier_mutex);
      offset_ring_buffer(&rb);
    }

    release_barrier();
  }
}

// Debugging
int Pipeline::save_pipeline(std::string path) {
  std::ofstream outfile(path, std::ios::binary);

  if (!outfile.is_open()) {
    std::cerr << "Error opening file for writing" << std::endl;
    return 1;
  }

  outfile.write(reinterpret_cast<char *>(rb.data),
                sizeof(float) * N_SENSORS * BUFFER_LENGTH);

  // Close the file
  outfile.close();

  return 0;
}
