// FeatureExtractionCpp.cpp : Defines the entry point for the console application.
//
#include "stdafx.h"
#include "HaemoragingImage.hpp"
#include "HistogramNormalize.hpp"

namespace fs = boost::filesystem;

const char* keys =
{
    "{ref||| must specify reference image name}"
    "{target||| must specify image name}"
    "{in|inputDir||directory to read files from}"
    "{out|outDir||output directory}"
    "{size||0|output image dimensions}"
    "{d|debug||invoke debugging functionality}"
    "{t|threshold|12|Canny threshold}"
    "{scale||1|image scale}"
    "{m|mask|1|should mask}"

};

Mat src;
RNG rng(12345);
string sourceWindow("Reference");
string targetWindow("Target");
string transformedWindow("Transformed");
string enhancedWindow("Enhanced");
string contouredWindow("Contoured");

ParamBag params;
unique_ptr<HaemoragingImage> haemorage(new HaemoragingImage);

void CreateDir(fs::path out_path)
{
    if (fs::exists(out_path))
    {
        fs::remove_all(out_path);
    }

    fs::create_directories(out_path);
}

void CreateMask(HaemoragingImage& hi, int thresh, Mat& mask)
{
    int i = 0;
    Mat image(hi.getImage()), origImage(hi.getImage());

    // we downsize the image before creating the mask.
    // due to the nature of our images, it makes sense
    // to lose some color info: this way the noise will mostly get lost
    // while the contours of the eye will still get preserved.
    // improves accuracy and speed
    Size origSize(image.size());
    resize(image, image, Size(256, 256));
    hi.setImage(image);
    hi.AddSaltAndPepper(0.05f);
    hi.MedianBlur(3);

    do
    {
        // in vast majority of cases this will only run once
        for (auto hull = hi.CreateEyeContours(i == 0 ? thresh : 1); hull.size() == 0 && thresh > 0; thresh--)
        {
            hull = hi.CreateEyeContours(i == 0 ? --thresh : 1);
        }
        
        mask = hi.CreateMask();
        i++;
    } while (hi.EyeAreaRatio() < 0.41 && i <= 2);
    resize(mask, mask, origSize);
    hi.setImage(origImage);
}

// color transfer experiments
void do_debug(CommandLineParser& parser)
{
    // keep for debugging
    string ref_file_name = parser.get<string>("ref");
    string file_name = parser.get<string>("target");
    int dim = parser.get<int>("size");
    int thresh = parser.get<int>("t");
    float scale = parser.get<float>("scale");
    int shouldMask = parser.get<int>("mask");

    Size size(dim, dim);

    // debugging stuff
    // load the target image & show it
    Mat rgb;
    rgb = imread(file_name, IMREAD_COLOR);

    // create the image mask to be used last
    auto hi = HaemoragingImage(rgb);

    Mat mask;
    if (shouldMask == 1)
    {
        CreateMask(hi, thresh, mask);
    }
    else
    {
        mask = Mat::zeros(hi.getImage().size(), CV_8UC1);
        mask = Scalar(255);
    }


    namedWindow(targetWindow, WINDOW_NORMAL);
    imshow(targetWindow, rgb);

    // load the reference image & show it
    Mat reference = imread(ref_file_name, IMREAD_COLOR);

    // show image contours and filter its background
    HaemoragingImage ref_haem(reference);

    Mat refMask;
    if (shouldMask == 1)
    {
        CreateMask(ref_haem, thresh, refMask);
    }
    else
    {
        refMask = Mat::zeros(ref_haem.getImage().size(), CV_8UC1);
        refMask = Scalar(255);
    }


    namedWindow(sourceWindow, WINDOW_NORMAL);
    imshow(sourceWindow, reference);

    Channels _channels[3] = { Channels::RED, Channels::GREEN, Channels::BLUE };
    vector<Channels> channels(_channels, _channels + 3);

    auto histSpec = HistogramNormalize(reference, refMask, channels);

    Mat dest;
    histSpec.HistogramSpecification(rgb, dest, mask);
    namedWindow(transformedWindow, WINDOW_NORMAL);
    imshow(transformedWindow, dest);

    //3. CLAHE
    hi.setImage(dest);
    if (shouldMask)
    {
        hi.MaskOffBackground(mask);
    }

    Mat hsv;
    cvtColor(hi.getEnhanced(), hsv, COLOR_BGR2HSV);

    hi.setImage(hsv);
    hi.GetOneChannelImages(Channels::V);
    hi.ApplyClahe();
    dest = hi.getEnhanced();
    
    cvtColor(dest, rgb, COLOR_HSV2BGR);

    if (size.width > 0)
    {
        resize(rgb, rgb, size);
    }

    namedWindow(enhancedWindow, WINDOW_NORMAL);
    imshow(enhancedWindow, rgb);

    //createTrackbar("Track", sourceWindow, &(params.cannyThresh), 100, thresh_callback);
    //thresh_callback(0, &(params.cannyThresh));
    //ref_image.DisplayEnhanced(true);
    waitKey(0);

}

//1. Pyramid Down
//2. Find the eye and get the mask
//3. Apply mask to filter background
//4. Histogram specification: 6535_left
//5. Histogram equalization (CLAHE) on V channel of the HSV image
//6. Resize to size x size
//7. Write to out_path
void process_files(string& ref, fs::path& in_path, vector<string>& in_files, fs::path& out_path, fs::path mask_path, Size& size, float scale = 1.0, int shouldMask = 1)
{
    int thresh = params.cannyThresh;
    bool doResize = size.width > 0;

    // process reference image
    Mat reference = imread(ref, IMREAD_COLOR);

    auto ref_image = HaemoragingImage(reference);

    ref_image.setImage(reference);
    Mat refMask;
    CreateMask(ref_image, thresh, refMask);

    Channels _channels[3] = { Channels::RED, Channels::GREEN, Channels::BLUE };
    vector<Channels> channels(_channels, _channels + 3);
    
    // create the class for histogram specification
    auto histSpec = HistogramNormalize(reference, refMask, channels);


    for (string& in_file : in_files)
    {
        Mat mask;
        Mat rgb;

        // in-path
        fs::path filePath = in_path / fs::path(in_file);
        // out-path
        fs::path outFilePath = out_path / fs::path(in_file);
        // mask path
        fs::path maskFilePath = mask_path / (filePath.stem().string() + string(".png"));

        // read it
        rgb = imread(filePath.string(), IMREAD_COLOR);
        HaemoragingImage hi(rgb);

        // 2. Find contours, get mask.
        // if we did not get the entire eye - reset the threhsold to 1 and repeat
        if (shouldMask == 1)
        {
            CreateMask(hi, thresh, mask);
        }
        else
        {
            mask = Mat::ones(rgb.size(), CV_8UC1);
            mask = Scalar(255);
        }


        // 3. Histogram specification
        Mat dest;
        histSpec.HistogramSpecification(rgb, dest, mask);

        // 4. CLAHE
        hi.setImage(dest);

        //5. Apply mask to filter background noise
        if (shouldMask)
        {
            hi.MaskOffBackground(mask);
        }

        Mat hsv;
        cvtColor(hi.getEnhanced(), hsv, COLOR_BGR2HSV);

        hi.setImage(hsv);
        hi.GetOneChannelImages(Channels::V);
        hi.ApplyClahe();

        // 6. covnert to RGB
        hsv = hi.getEnhanced();
        cvtColor(hsv, dest, COLOR_HSV2BGR);

        if (doResize)
        {
            resize(dest, dest, size, INTER_AREA);
            resize(mask, mask, size, INTER_AREA);
        }
        else if (scale != 1.0)
        {
            int cols = (int)(dest.cols / scale);
            int rows = (int)(dest.rows / scale);
            Size scaled = Size(cols, rows);
            resize(dest, dest, scaled, INTER_AREA);
            resize(mask, mask, scaled, INTER_AREA);
        }

         // 7. write out
        imwrite(outFilePath.string(), dest);
        imwrite(maskFilePath.string(), mask);
    }
}

int main(int argc, char** argv)
{
    CommandLineParser parser(argc, argv, keys);

    string in_dir = parser.get<string>("in");
    string out_dir = parser.get<string>("out");
    string ref = parser.get<string>("ref");
    params.cannyThresh = parser.get<int>("t");
    float scale = parser.get<float>("scale");
    int shouldMask = parser.get<int>("mask");


    int dim = parser.get<int>("size");
    Size size(dim, dim);
    
    bool debug = parser.get<bool>("debug");

    if (debug)
    {
        do_debug(parser);
        return 0;
    }

    fs::path in_path(in_dir);
    fs::path out_path(out_dir);
    fs::path mask_path(out_dir + "masks");

    CreateDir(out_path);
    CreateDir(mask_path);

    // print out GPU information
    gpu::printCudaDeviceInfo(0);
    fs::directory_iterator it(in_path), enumer(in_path);

    vector<string> in_files;
    
    DIR *dir;
    struct dirent *ent;

    // using dirent because it actually finishes (unlike boost)
    dir = opendir(in_dir.c_str());
    if (dir != NULL)
    {
        /* Print all files and directories within the directory */
        while ((ent = readdir(dir)) != NULL)
        {
            if (ent->d_type == DT_REG)
            {
                in_files.push_back(ent->d_name);
            }
        }
    }
    closedir(dir);

    Stopwatch sw;
    process_files(ref, in_path, in_files, out_path, mask_path, size, scale, shouldMask);
    sw.tick();
    cout << "Elapsed: " << sw.Elapsed() << "sec." << endl;
    return(0);
}

