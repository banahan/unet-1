/* Copyright (c) 2019 Intel Corporation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "../include/brainunetopenvino.h"

void BrainUnetOpenVino::loadData(int img_num, std::vector<double> &temp_data,
                                 std::vector<double> &orig_gt_msks) {
  // reading numpy images
  cnpy::NpyArray arr;
  cnpy::NpyArray arr_msks;

  arr = cnpy::npz_load(DATA_FILENAME, "imgs_validation");
  arr_msks = cnpy::npz_load(DATA_FILENAME, "msks_validation");
  std::cout << "Numpy arrays loaded" << std::endl;

  double *loaded_data = arr.data<double>();
  double *loaded_data_msks = arr_msks.data<double>();

  input_shape.NN = arr.shape[0];
  input_shape.NH = arr.shape[1];
  input_shape.NW = arr.shape[2];
  input_shape.NC = arr.shape[3];

  output_shape.NN = arr_msks.shape[0];
  output_shape.NH = arr_msks.shape[1];
  output_shape.NW = arr_msks.shape[2];
  output_shape.NC = arr_msks.shape[3];

  size_t img_index;
  if (img_num < 0) {
    img_index = 0;
  } else {
    if (img_num > input_shape.NN) {
      img_index = input_shape.NN
    } else {
      img_index = img_num
    }
  };

  std::cout << "Input Shape: H=" << input_shape.NH << ", W=" << input_shape.NW
            << ", C=" << input_shape.NC << ", N=" << input_shape.NN
            << std::endl;
  std::cout << "Output Shape: H=" << output_shape.NH
            << ", W=" << output_shape.NW << ", C=" << output_shape.NC
            << ", N=" << output_shape.NN << std::endl;

  // Store the imgs for validation from numpy array into 1-D array
  size_t start_i = input_shape.NH * input_shape.NW * input_shape.NC * img_index;
  size_t end_i =
      input_shape.NH * input_shape.NW * input_shape.NC * (img_index + 1);
  for (size_t i = start_i; i < end_i; ++i) {
    double t = loaded_data[i];
    temp_data.push_back(t);
  }
  // Store the gt masks from numpy array into 1-D array
  start_i = output_shape.NH * output_shape.NW * output_shape.NC * img_index;
  end_i = output_shape.NH * output_shape.NW * output_shape.NC * (img_index + 1);
  for (size_t i = start_i; i < end_i; ++i) {
    double t_msks = loaded_data_msks[i];
    orig_gt_msks.push_back(t_msks);
  }
  std::cout << "Finished reading Numpy arrays " << std::endl;
}

void BrainUnetOpenVino::makeInference(
    int img_num, InferenceEngine::TargetDevice targetDevice) {

  // reading numpy images
  std::vector<double> temp_data;
  std::vector<double> orig_gt_msks;
  loadData(img_num, temp_data, orig_gt_msks);

  // Initialize Engine plugin, choose plugin type and set precision
  InferenceEnginePluginPtr engine_ptr;
  engine_ptr =
      PluginDispatcher({M_IE_PLUGIN_PATH, ""}).getSuitablePlugin(targetDevice);

  std::cout << "suitable plugin received" << std::endl;
  InferencePlugin plugin(engine_ptr);
  std::cout << "** Create InferenceEngine plugin." << std::endl;
  std::string target_precision;
  if (targetDevice == TargetDevice::eCPU)
    target_precision = "FP32";
  else {
    if (targetDevice == TargetDevice::eMYRIAD)
      target_precision = "FP16";
  }
  std::string network_model_path =
      MODEL_DIR + target_precision + "/" + MODEL_FILENAME + ".xml";
  std::string network_weights_path =
      "../models/" + target_precision + "/" + MODEL_FILENAME + ".bin";
  std::cout << network_weights_path << std::endl;

  /******* Load and read the networks *****/
  CNNNetReader network_reader;
  network_reader.ReadNetwork(network_model_path);
  network_reader.ReadWeights(network_weights_path);
  std::cout << "** Loaded pretrained network." << std::endl;
  CNNNetwork network;
  network = network_reader.getNetwork();
  std::cout << "** Retrieved network." << std::endl;

  // ----------------------------
  //  3. Configure input & output
  // ----------------------------
  // -- Prepare input blobs
  // ----------------------
  std::cout << "Preparing input blobs" << std::endl;
  /** Taking information about all topology inputs **/
  InputsDataMap input_info(network.getInputsInfo());
  if (input_info.size() != 1)
    throw std::logic_error("Sample supports topologies only with 1 input");
  for (auto &item : input_info) {
    InputInfo::Ptr &input_data = item.second;
    input_data->setPrecision(Precision::FP32);
    input_data->setLayout(Layout::NHWC);
  }
  std::cout << "** Input has been configured." << std::endl;

  // -- Prepare output blobs
  // -----------------------
  std::cout << "Preparing output blobs" << std::endl;
  OutputsDataMap output_info(network.getOutputsInfo());
  std::string firstOutputName;
  for (auto &item : output_info) {
    if (firstOutputName.empty()) {
      firstOutputName = item.first;
    }
    DataPtr &output_data = item.second;
    if (!output_data) {
      throw std::logic_error("output data pointer is not valid");
    }
    output_data->setPrecision(Precision::FP32);
    output_data->setLayout(Layout::NHWC);
  }

  std::cout << "** Output has been configured." << std::endl;

  // 4. Loading model to the plugin
  // ------------------------------
  // std::cout << "Loading model to the plugin" << std::endl;
  ExecutableNetwork executable_network = plugin.LoadNetwork(network, {});
  std::cout << "** Executable network has been created." << std::endl;

  // 5. Create infer request
  // -----------------------
  InferRequest infer_request = executable_network.CreateInferRequest();
  std::cout << "** Inference request has been created." << std::endl;

  // 6. Prepare input
  // ----------------
  /** Iterate over all the input blobs **/
  Blob::Ptr inputBlob;
  for (auto &item : input_info)
    inputBlob = infer_request.GetBlob(item.first);
  // InferenceEngine::SizeVector blobSize =
  // inputBlob->getTensorDesc().getDims();
  auto blob_data =
      inputBlob->buffer().as<PrecisionTrait<Precision::FP32>::value_type *>();

  for (size_t c1 = 0; c1 < (input_shape.NH * input_shape.NW * input_shape.NC);
       ++c1) {
    blob_data[c1] = temp_data[c1];
  }
  SizeVector inputShape = inputBlob->dims();

  // 7. Do inference
  // ---------------
  // start inference time
  auto start = std::chrono::high_resolution_clock::now();
  infer_request.Infer();
  // std::cout << "** Inference request has been started." << std::endl;

  // 8. Process output
  // -----------------
  // std::cout << "Processing output blobs" << std::endl;

  const Blob::Ptr output_blob = infer_request.GetBlob(firstOutputName);
  const auto predicted_output_msk =
      output_blob->buffer().as<PrecisionTrait<Precision::FP32>::value_type *>();
  auto finish = std::chrono::high_resolution_clock::now();
  float inf_time =
      (std::chrono::duration_cast<std::chrono::milliseconds>(finish - start)
           .count());
  std::cout << "Inference Done; inference time " << inf_time << " msec"
            << std::endl;

  // 9. Compute some metrics from segmentation output and Display results
  // read corresponding GT and actual brain image from the image directories
  // change paths

  cv::Mat output_pred_img =
      cv::Mat(cv::Size(input_shape.NH, input_shape.NH), CV_8UC1);
  cv::Mat output_GT_msks =
      cv::Mat(cv::Size(output_shape.NH, output_shape.NH), CV_8UC1);

  int cnt = 0;
  int predicted_cnt = 0;
  int gt_cnt = 0;
  float intersection = 0.0f;
  float total_union = 0.0f;
  float dice_coeff = 0.0f;
  for (size_t cnt = 0;
       cnt < (output_shape.NH * output_shape.NW * output_shape.NC); ++cnt) {
    if (predicted_output_msk[cnt] > 0.5) {
      predicted_cnt++;
      if (orig_gt_msks[cnt] > 0.0) {
        gt_cnt++;
        intersection = intersection + 1;
      }

      else {
        total_union = total_union + 1;
      }
    } else {
      if (orig_gt_msks[cnt] > 0.0) {
        gt_cnt++;
        total_union = total_union + 1;
      }
    }
  }
  cnt = 0;
  for (size_t h = 0; h < output_shape.NH; ++h) {
    for (size_t w = 0; w < output_shape.NW; ++w) {

      // the threshold is currently set to 0.5
      if (predicted_output_msk[cnt] > 0.5) {
        output_pred_img.at<uchar>(h, w) = 255;
        if (orig_gt_msks[cnt] > 0.0) {
          output_GT_msks.at<uchar>(h, w) = 255;

        } else {

          output_GT_msks.at<uchar>(h, w) = 0;
        }
      }

      else {
        output_pred_img.at<uchar>(h, w) = 0;
        if (orig_gt_msks[cnt] > 0.0) {
          output_GT_msks.at<uchar>(h, w) = 255;
        } else
          output_GT_msks.at<uchar>(h, w) = 0;
      }
      cnt++;
    }
  }

  // compute Dice coefficient
  if (predicted_cnt == 0) {
    std::cout << "No Tumor found "
              << "\n";
  } else {
    dice_coeff = ((2.0f * intersection) + 1) / (gt_cnt + predicted_cnt + 1);
    std::cout << "dice_coeff  " << dice_coeff << "\n";
  }
  cv::imshow("GT image", output_GT_msks);
  cv::waitKey(0);
  cv::imshow("Predicted mask image", output_pred_img);
  cv::waitKey(0);
}
