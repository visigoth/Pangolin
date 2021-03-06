#include <pangolin/pangolin.h>
#include <pangolin/video/video_record_repeat.h>
#include <pangolin/gl/gltexturecache.h>
#include <pangolin/gl/glpixformat.h>
#include <pangolin/handler/handler_image.h>
#include <pangolin/utils/file_utils.h>

void VideoViewer(const std::string& input_uri, const std::string& output_uri)
{
    int frame = 0;
    pangolin::Var<int>  end_frame("viewer.end_frame", std::numeric_limits<int>::max() );

    // Open Video by URI
    pangolin::VideoRecordRepeat video(input_uri, output_uri);
    const size_t num_streams = video.Streams().size();
    int total_frames = std::numeric_limits<int>::max();

    if(num_streams == 0) {
        pango_print_error("No video streams from device.\n");
        return;
    }

    // Check if video supports VideoPlaybackInterface
    pangolin::VideoPlaybackInterface* video_playback = video.Cast<pangolin::VideoPlaybackInterface>();
    if( video_playback ) {
        total_frames = video_playback->GetTotalFrames();
        if(total_frames < std::numeric_limits<int>::max() ) {
            std::cout << "Video length: " << total_frames << " frames" << std::endl;
            end_frame = 1;
        }
    }

    std::vector<unsigned char> buffer;
    buffer.resize(video.SizeBytes()+1);

    // Create OpenGL window - guess sensible dimensions
    pangolin::CreateWindowAndBind( "VideoViewer",
        video.Width() * num_streams, video.Height()
    );

    // Assume packed OpenGL data unless otherwise specified
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    // Setup resizable views for video streams
    std::vector<pangolin::GlPixFormat> glfmt;
    std::vector<std::pair<float,float> > gloffsetscale;
    std::vector<pangolin::ImageViewHandler> handlers;
    handlers.reserve(num_streams);

    pangolin::View& container = pangolin::Display("streams");
    container.SetLayout(pangolin::LayoutEqual);
    for(unsigned int d=0; d < num_streams; ++d) {
        const pangolin::StreamInfo& si = video.Streams()[d];
        pangolin::View& view = pangolin::CreateDisplay().SetAspect(si.Aspect());
        container.AddDisplay(view);
        glfmt.push_back(pangolin::GlPixFormat(si.PixFormat()));
        gloffsetscale.push_back(std::pair<float,float>(0.0f, 1.0f) );
        handlers.push_back( pangolin::ImageViewHandler(si.Width(), si.Height()) );
        view.SetHandler(&handlers.back());
    }

    std::vector<pangolin::Image<unsigned char> > images;

#ifdef CALLEE_HAS_CPP11
    const int FRAME_SKIP = 30;
    const char show_hide_keys[]  = {'1','2','3','4','5','6','7','8','9'};
    const char screenshot_keys[] = {'!','"','#','$','%','^','&','*','('};

    // Show/hide streams
    for(size_t v=0; v < container.NumChildren() && v < 9; v++) {
        pangolin::RegisterKeyPressCallback(show_hide_keys[v], [v,&container](){
            container[v].ToggleShow();
        } );
        pangolin::RegisterKeyPressCallback(screenshot_keys[v], [v,&images,&video](){
            if(v < images.size() && images[v].ptr) {
                try{
                    pangolin::SaveImage(
                        images[v], video.Streams()[v].PixFormat(),
                        pangolin::MakeUniqueFilename("capture.png")
                    );
                }catch(std::exception e){
                    pango_print_error("Unable to save frame: %s\n", e.what());
                }
            }
        } );
    }

    pangolin::RegisterKeyPressCallback('r', [&](){
        if(!video.IsRecording()) {
            video.Record();
            pango_print_info("Started Recording.\n");
        }else{
            video.Stop();
            pango_print_info("Finished recording.\n");
        }
        fflush(stdout);
    });
    pangolin::RegisterKeyPressCallback('p', [&](){
        video.Play();
        end_frame = std::numeric_limits<int>::max();
        pango_print_info("Playing from file log.\n");
        fflush(stdout);
    });
    pangolin::RegisterKeyPressCallback('s', [&](){
        video.Source();
        end_frame = std::numeric_limits<int>::max();
        pango_print_info("Playing from source input.\n");
        fflush(stdout);
    });
    pangolin::RegisterKeyPressCallback(' ', [&](){
        end_frame = (frame < end_frame) ? frame : std::numeric_limits<int>::max();
    });
    pangolin::RegisterKeyPressCallback(',', [&](){
        if(video_playback) {
            const int frame = std::min(video_playback->GetCurrentFrameId()-FRAME_SKIP, video_playback->GetTotalFrames()-1);
            video_playback->Seek(frame);
        }else{
            // We can't go backwards
        }
    });
    pangolin::RegisterKeyPressCallback('.', [&](){
        if(video_playback) {
            const int frame = std::max(video_playback->GetCurrentFrameId()+FRAME_SKIP, 0);
            video_playback->Seek(frame);
        }else{
            // Pause at this frame
            end_frame = frame+1;
        }
    });
    pangolin::RegisterKeyPressCallback('a', [&](){
        // Adapt scale
        for(unsigned int i=0; i<images.size(); ++i) {
            if(container[i].HasFocus()) {
                pangolin::Image<unsigned char>& img = images[i];
                pangolin::ImageViewHandler& ivh = handlers[i];

                const bool have_selection = std::isfinite(ivh.GetSelection().Area()) && std::abs(ivh.GetSelection().Area()) >= 4;
                pangolin::XYRangef froi = have_selection ? ivh.GetSelection() : ivh.GetViewToRender();
                gloffsetscale[i] = pangolin::GetOffsetScale(img, froi.Cast<int>(), glfmt[i]);
            }
        }
    });
    pangolin::RegisterKeyPressCallback('g', [&](){
        std::pair<float,float> os_default(0.0f, 1.0f);

        // Get the scale and offset from the container that has focus.
        for(unsigned int i=0; i<images.size(); ++i) {
            if(container[i].HasFocus()) {
                pangolin::Image<unsigned char>& img = images[i];
                pangolin::ImageViewHandler& ivh = handlers[i];

                const bool have_selection = std::isfinite(ivh.GetSelection().Area()) && std::abs(ivh.GetSelection().Area()) >= 4;
                pangolin::XYRangef froi = have_selection ? ivh.GetSelection() : ivh.GetViewToRender();
                os_default = pangolin::GetOffsetScale(img, froi.Cast<int>(), glfmt[i]);
                break;
            }
        }

        // Adapt scale for all images equally
        // TODO : we're assuming the type of all the containers images' are the same.
        for(unsigned int i=0; i<images.size(); ++i) {
            gloffsetscale[i] = os_default;
        }

    });
#endif // CALLEE_HAS_CPP11


    // Stream and display video
    while(!pangolin::ShouldQuit())
    {
        glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
        glColor3f(1.0f, 1.0f, 1.0f);

        if (frame == 0 || frame < end_frame) {
            if (video.Grab(&buffer[0], images) ){
                ++frame;
            }
        }

        glLineWidth(1.5f);
        glDisable(GL_DEPTH_TEST);

        for(unsigned int i=0; i<images.size(); ++i)
        {
            if(container[i].IsShown()) {
                container[i].Activate();
                pangolin::Image<unsigned char>& image = images[i];

                // Get texture of correct dimension / format
                const pangolin::GlPixFormat& fmt = glfmt[i];
                pangolin::GlTexture& tex = pangolin::TextureCache::I().GlTex(image.w, image.h, fmt.scalable_internal_format, fmt.glformat, fmt.gltype);

                // Upload image data to texture
                tex.Bind();
                tex.Upload(image.ptr,0,0, image.w, image.h, fmt.glformat, fmt.gltype);

                // Render
                handlers[i].UpdateView();
                handlers[i].glSetViewOrtho();
                const std::pair<float,float> os = gloffsetscale[i];
                pangolin::GlSlUtilities::OffsetAndScale(os.first, os.second);
                handlers[i].glRenderTexture(tex);
                pangolin::GlSlUtilities::UseNone();
                handlers[i].glRenderOverlay();
            }
        }

        pangolin::FinishFrame();
    }
}


int main( int argc, char* argv[] )
{
    const std::string dflt_output_uri = "pango://video.pango";

    if( argc > 1 ) {
        const std::string input_uri = std::string(argv[1]);
        const std::string output_uri = (argc > 2) ? std::string(argv[2]) : dflt_output_uri;
        try{
            VideoViewer(input_uri, output_uri);
        } catch (pangolin::VideoException e) {
            std::cout << e.what() << std::endl;
        }
    }else{
        const std::string input_uris[] = {
            "dc1394:[fps=30,dma=10,size=640x480,iso=400]//0",
            "convert:[fmt=RGB24]//v4l:///dev/video0",
            "convert:[fmt=RGB24]//v4l:///dev/video1",
            "openni:[img1=rgb]//",
            "test:[size=160x120,n=1,fmt=RGB24]//"
            ""
        };

        std::cout << "Usage  : VideoViewer [video-uri]" << std::endl << std::endl;
        std::cout << "Where video-uri describes a stream or file resource, e.g." << std::endl;
        std::cout << "\tfile:[realtime=1]///home/user/video/movie.pvn" << std::endl;
        std::cout << "\tfile:///home/user/video/movie.avi" << std::endl;
        std::cout << "\tfiles:///home/user/seqiemce/foo%03d.jpeg" << std::endl;
        std::cout << "\tdc1394:[fmt=RGB24,size=640x480,fps=30,iso=400,dma=10]//0" << std::endl;
        std::cout << "\tdc1394:[fmt=FORMAT7_1,size=640x480,pos=2+2,iso=400,dma=10]//0" << std::endl;
        std::cout << "\tv4l:///dev/video0" << std::endl;
        std::cout << "\tconvert:[fmt=RGB24]//v4l:///dev/video0" << std::endl;
        std::cout << "\tmjpeg://http://127.0.0.1/?action=stream" << std::endl;
        std::cout << "\topenni:[img1=rgb]//" << std::endl;
        std::cout << std::endl;

        // Try to open some video device
        for(int i=0; !input_uris[i].empty(); ++i )
        {
            try{
                pango_print_info("Trying: %s\n", input_uris[i].c_str());
                VideoViewer(input_uris[i], dflt_output_uri);
                return 0;
            }catch(pangolin::VideoException) { }
        }
    }

    return 0;
}
