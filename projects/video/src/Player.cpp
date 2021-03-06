#include <video/Player.h>
#include <errno.h>
#include <unistd.h>

namespace vid {
  
  /* ---------------------------------------------------------------------------------- */

  static void* player_thread(void* user);
  static void player_on_decoded_frame(AVFrame* frame, void* user);
  static void player_on_play_frame(AVFrame* frame, void* user);
  static void player_on_video_event(int event, void* user);

  /* ---------------------------------------------------------------------------------- */

  Player::Player() 
    :must_stop(true)
    ,must_shutdown(false)
    ,is_running(false)
    ,user(NULL)
    ,on_frame(NULL)
    ,on_event(NULL)
  {
    /* init mutex. */
    if (0 != pthread_mutex_init(&mutex, NULL)) {
      RX_ERROR("Cannot init mutex");
    }
  }

  Player::~Player() {

    int r;

    if (is_running) {
      RX_VERBOSE("stop runing!");
      shutdown();
    }

    /* destroy the mutex. */
    r = pthread_mutex_destroy(&mutex);
    if (r != 0) {
      RX_ERROR("Cannot destory the mutex: %d, %s", r, strerror(errno));
    }

    stream.user = NULL;
    stream.on_frame = NULL;
    jitter.on_frame = NULL;
    jitter.user = NULL;
    must_shutdown = false;
    must_stop = true;
    is_running = false;
    user = NULL;
    on_event = NULL;
  }

  int Player::init(std::string surl) {

    if (true == is_running) {
      RX_ERROR("The player is already initialized. Call shutdown() first.");
      return -100;
    }

    /* init members */
    must_stop = false;
    url = surl;
    is_running = true;
    stream.user = this;
    stream.on_frame = player_on_decoded_frame;
    stream.on_event = player_on_video_event;
    jitter.on_event = player_on_video_event;
    jitter.on_frame = player_on_play_frame;
    jitter.user = this;

    /* start the thread */
    if (0 != pthread_create(&thread, NULL, player_thread, this)) {
      RX_ERROR("Cannot create player thread");
      return -102;
    }

    return 0;
  }

  int Player::update() {

    /* silently ignore updates when we're stopped. */
    if (true == must_stop || false == is_running) {
      return 0;
    }

    if (true == must_shutdown) {
      shutdown();
      must_shutdown = false;
      return 0;
    }

    lock();
      jitter.update();
    unlock();

    return 0;
  }

  int Player::stop() {

    if (false == is_running) {
      RX_VERBOSE("Cannot stop the player; not running.");
      return -1;
    }

    if (0 != shutdown()) {
      return -2;
    }

    return 0;
  }

  int Player::play() {

    if (0 == url.size()) {
      RX_ERROR("No url set ot playback");
      return -1;
    }

    return init(url);
  }

  int Player::shutdown() {
    int r;

    if (false == is_running) {
      RX_VERBOSE("Not joining player thread, already stopped.");
      return 0;
    }

    if (true == must_stop) {
      RX_VERBOSE("Already shutting down the player thread.");
      return 0;
    }

    must_stop = true;

    RX_VERBOSE("Joining stream thread.");

    r = pthread_join(thread, NULL);
    if (0 != r) {
      RX_WARNING("Failed to join the player thread: %d, %s", r, strerror(r));
    }
    is_running = false;

    if (0 != jitter.shutdown()) {
      RX_ERROR("Error while trying to shut down the Jitter.");
    }

    if (0 != stream.shutdown()) {
      RX_ERROR("Error while trying to shutdown the Stream");
    }
   
    /* notify user. */
    if (NULL != on_event) {
      on_event(this, VID_EVENT_SHUTDOWN);
    }
    return 0;
  }

  /* ---------------------------------------------------------------------------------- */

  static void* player_thread(void* user) {

    Player* p;
    int r;

    /* get a handle to the player. */
    p = static_cast<Player*>(user);
    if (NULL == p) {
      RX_ERROR("Invalid user pointer in player thread - not supposed to happen!");
      return NULL;
    }

    /* start the stream */
    r = p->stream.init(p->url);
    if (0 != r) {
      p->is_running = false;
      if (p->on_event) {
        p->on_event(p, VID_EVENT_INIT_ERROR);
      }
      return NULL;
    }

    /* start the jitter buffer */
    r = p->jitter.init(p->stream.video_stream_timebase);
    if (0 != r) {
      p->is_running = false;
      if (0 != p->stream.shutdown()) {
        RX_ERROR("Cannot init jitter, so also closing the stream, but that failed.");
      }
      if (p->on_event) {
        p->on_event(p, VID_EVENT_INIT_ERROR);
      }
      return NULL;
    }

    /* our thread loop */
    while (false == p->must_stop && false == p->must_shutdown) {
      p->stream.update();
    }

    RX_VERBOSE("Player thread stopped/returning");

    return NULL;
  }

  static void player_on_decoded_frame(AVFrame* frame, void* user) {
    
    /* get ref to the player. */
    Player* p = static_cast<Player*>(user);
    if (NULL == p) {
      RX_ERROR("User pointer is invalid");
      return;
    }

    if (true == p->must_stop) {
      RX_VERBOSE("Got a decoded frame; but the player is shut down.");
      /* we will free the frame here because if it's still referenced libav 
         won't cleanup memory at all */
      av_frame_free(&frame);
      return;
    }

    p->lock();
      p->jitter.addFrame(frame);
    p->unlock();
  }

  static void player_on_play_frame(AVFrame* frame, void* user) {

    if (NULL == frame) {
      RX_ERROR("Invalid frame given");
      return;
    }

    Player* p = static_cast<Player*>(user);
    if (NULL == p) {
      RX_ERROR("Player is NULL");
      return;
    }

    if (AV_PIX_FMT_YUV420P != frame->format) {
      RX_ERROR("Frame as invalid pixel format");
      return;
    }

    if (NULL == p->on_frame) {
      RX_ERROR("Makes no sense to use the video player and not set a on_frame handler :)");
      return;
    }

    p->on_frame(frame, p->user);
  }

  /* can be called from another thread, so we set flags that update the main/user thread. */
  static void player_on_video_event(int event, void* user) {

    /* get the player. */
    Player* p = static_cast<Player*>(user);
    if (NULL == p) {
      RX_ERROR("Cannot get player pointer in video event. This should not happen");
      return;
    }

    if (VID_EVENT_STOP_PLAYBACK == event) {
      RX_VERBOSE("Received a VID_EVENT_STOP_PLAYBACK");
      p->must_shutdown = true;
    }
    else if (VID_EVENT_EOF == event) {
      RX_VERBOSE("Received a VID_EVENT_EOF");
      p->must_shutdown = true;
    }
    else if (VID_EVENT_TIMEOUT == event) {
      RX_VERBOSE("Received a VID_EVENT_TIMEOUT.");
      p->must_shutdown = true;
    }
    else if (VID_EVENT_INIT_SUCCESS == event) {
      RX_VERBOSE("Received a VID_EVENT_INIT_SUCCES.");
      if (p->on_event) {
        p->on_event(p, event);
      }
    }
  }

  /* ---------------------------------------------------------------------------------- */

} /* namespace vid */
