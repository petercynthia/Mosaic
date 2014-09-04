#include <featurex/Config.h>
#include <topshop/Config.h>
#include <topshop/TopShop.h>

namespace top {

  /* ------------------------------------------------------------------------- */
  
  static void on_new_file(ImageCollector* col, CollectedFile& file);         /* gets called when a file is ready to be shown in the mosaic. */

  /* ------------------------------------------------------------------------- */
  
  TopShop::TopShop() 
#if USE_GRID
    :left_grid(GRID_DIR_RIGHT)
    ,right_grid(GRID_DIR_LEFT)
#endif
  {
  }

  TopShop::~TopShop() {

  }

  int TopShop::init() {
    int r = 0;

    /* init the mosaic. */
    r = mosaic.init();
    if (0 != r) {
      RX_ERROR("Cannot init mosiac: %d", r);
      return -101;
    }

    /* init the dir watcher. */
    r = img_collector.init(fex::config.raw_filepath);
    if (0 != r) {
      RX_ERROR("Cannot start the image collector: %d.", r);
      mosaic.shutdown();
      return -102;
    }

    img_collector.user = this;
    img_collector.on_file = on_new_file;


    return 0;
  }

  int TopShop::shutdown() {
    int r = 0;

    r = mosaic.shutdown();
    if (0 != r) {
      RX_ERROR("Cannot shutdown the mosaic: %d", r);
    }

    r = img_collector.shutdown();
    if (0 != r) {
      RX_ERROR("Failed to shutdown the image collector: %d.", r);
    }
    
    return 0;
  }

  void TopShop::update() {
    img_collector.update();
    mosaic.update();
  }

  void TopShop::draw() {

    mosaic.draw(top::config.mosaic_x, top::config.mosaic_y, top::config.mosaic_width, top::config.mosaic_height);

    if (1 == top::config.is_debug_draw) {
      mosaic.debugDraw();
    }
  }

  /* ------------------------------------------------------------------------- */

  static void on_new_file(ImageCollector* col, CollectedFile& file) {

    if (NULL == col) {
      RX_ERROR("Invalid ImageCollector ptr.");
      return;
    }
    
    TopShop* shop = static_cast<TopShop*>(col->user);
    if (NULL == shop) {
      RX_ERROR("Cannot cast the user member of the ImageCollector.");
      return;
    }
    
    std::string filepath = file.dir +"/" +file.filename;

#if !defined(NDEBUG)
    if (false == rx_file_exists(filepath)) {
      RX_ERROR("Filepath doesn't exists: %s", filepath.c_str());
      return;
    }
#endif



    /* @todo - we assume the file is correct here, we may add validation here to be sure */
    RX_VERBOSE("Got a mosaic file: %s", file.filename.c_str());

    file.type = COL_FILE_TYPE_RAW;

    if (0 != shop->mosaic.analyzeCPU(filepath)) {
      RX_ERROR("Failed to add a new file for the cpu analyzer. Check messages above");
    }
  } 

  /* ------------------------------------------------------------------------- */
  
} /* namespace top */