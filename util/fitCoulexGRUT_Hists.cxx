#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <glob.h>

#include "TF1.h"
#include "FitDoubleExpo_Gaus.C"
#include "TEnv.h"
#include "TH1.h"
#include "TH1F.h"
#include "TFile.h"
#include "TMath.h"
#include "TTree.h"
#include "TGraph.h"
#include "TCanvas.h"
#include "TLine.h"
#include "TCutG.h"
#include "TChannel.h"
#include "GValue.h"

#include "TCaesarHit.h"
#include "TS800Hit.h"
#include "TCaesar.h"
#include "TS800.h"

//This program takes as input a config file containing:
//    -  directory containing necessary input root tree files (INPUT_DIR)
//    -  number of parameters to use in the fit (NUM_PARS)
//    -  set of initial parameters for the fit (PAR.#) where # starts with 0
//    -  file containing cuts (CUT_FILE_NAME)
//    -  Ranges for histogramming (DATA_LOW_X, DATA_HIGH_X)
//    -  Ranges for fitting       (FIT_LOW_X,  FIT_HIGH_X)
//    -  Binning (KEV_PER_BIN)

//All files must contain a tree called "EventTree", i.e. they must be made by GRUTinizer.
//Usage: fitCoulex [CONFIG FILE]



void getFileList(std::string &search_string, std::vector<std::string> &file_list){
  glob_t glob_result; 

  if (glob(search_string.c_str(),GLOB_ERR,NULL,&glob_result) == GLOB_NOMATCH){
    std::cout << "GLOB failed to find pattern matching: " << search_string << std::endl;
    return;
  }

  std::cout << "Found " << glob_result.gl_pathc << " files matching " << search_string << ":"<<  std::endl;
  for (unsigned int i = 0; i < glob_result.gl_pathc; ++i){
    file_list.push_back( glob_result.gl_pathv[i] ); 
    std::cout << "\t" << glob_result.gl_pathv[i] << std::endl;
  }
 
  return;
}

void getHistograms(std::vector<TH1F*> &data_hists, std::vector<double> &angles,
                   TFile *&input_file, const double START_ANGLE, 
                   const double FINAL_ANGLE,  const double ANGLE_STEPS,
                   int data_low_x, int data_high_x, int kev_per_bin){
  const int TOTAL_ANGLES = (FINAL_ANGLE-START_ANGLE)/ANGLE_STEPS + 1;
  double temp_angle = START_ANGLE;
  for (int i = 0; i < TOTAL_ANGLES; i++){
    std::stringstream ss;
    ss << "fit_angle_" << temp_angle;
    std::string data_hist_name = ss.str();
    data_hists.push_back((TH1F*)input_file->Get(data_hist_name.c_str()));
    if (data_hists.at(i)->GetNbinsX() != data_hists.at(i)->GetXaxis()->GetXmax()){
      std::cout << "Number of bins ("<<data_hists.at(i)->GetNbinsX()<<") does not equal x-axis length ("
                << data_hists.at(i)->GetXaxis()->GetXmax()<<") so should not rebin!" << std::endl;
      exit(1);
    }
    data_hists.at(i)->GetXaxis()->SetRangeUser(data_low_x,data_high_x);
    data_hists.at(i)->Rebin(kev_per_bin);
    angles.push_back(temp_angle);
    temp_angle += ANGLE_STEPS;
  }
}

void getGeantHistograms(std::vector<TH1F*> &geant_hists, std::vector<std::string> &geant_file_names){
  for (unsigned int i = 0; i < geant_file_names.size(); i++){
    TFile *geant_file = new TFile(geant_file_names.at(i).c_str(),"read");
    if (!geant_file){
      std::cout << "Failed loading geant file: " << geant_file_names.at(i) << std::endl;
      return;
    }
    std::string geant_hist_name("all_dop");
    std::string geant_fep_hist_name("all_dop_fep");

    geant_hists.push_back( (TH1F*)geant_file->Get(geant_hist_name.c_str()));
    TH1F *geant_fep_hist = (TH1F*)geant_file->Get(geant_fep_hist_name.c_str());
    
    TH1F *geant_compton = (TH1F*)geant_hists.at(i*2)->Clone("geant_compton");
    geant_compton->Add(geant_fep_hist,-1);
    geant_hists.push_back(geant_compton);
  }
  return;
}

void getFiles(std::vector<TFile*> &files, std::vector<std::string> &file_list){
  for (unsigned int i = 0; i < file_list.size(); i++){
    files.push_back(new TFile(file_list.at(i).c_str(),"read"));
  }
  if (files.size() != file_list.size()){
    std::cout << "Unexpected number of trees: "   << files.size() 
              << " compared to number of files: " << file_list.size() << std::endl;
  }
  return;
}
int fitCoulex(const char *cfg_file_name){
   
  //////////////////////////////////////////////////
  //All Variables
  const int MAX_PARS = 10;
  //These angles determine the maximum angle cut that will be used
  const double START_ANGLE = 3.2;
  const double FINAL_ANGLE = 3.2;
  const double ANGLE_STEPS = 0.1;
  const int TOTAL_ANGLES = (FINAL_ANGLE-START_ANGLE)/ANGLE_STEPS + 1;

  double init_pars[MAX_PARS];
  for (int i = 0; i < MAX_PARS; i++){
    init_pars[i] = 0;
  }

  TEnv *cfg = new TEnv(cfg_file_name);
  std::vector<std::string> file_list;
  std::ofstream out_file;

  std::vector<TFile*> files;
  std::vector<TH1F*> data_hists;
  //the structure of the geant vector will be:
  //geant_hists.at(0) = full geant hist
  //geant_hists.at(1) = compton only (full - fep)
  //This will repeat. So if there's more than one geant
  //histogram, the next will be .at(2) and .at(3) will be compton
  std::vector<TH1F*> geant_hists;
  std::vector<double> angles;
  data_hists.reserve(TOTAL_ANGLES);//Want one histogram for every angle cut
  angles.reserve(TOTAL_ANGLES);

  //Fitting Arrays
  double hist_constant[TOTAL_ANGLES][2];
  double fit_error[TOTAL_ANGLES];
  double chi_squared[TOTAL_ANGLES];

  //Config File Settings
  std::string isotope_name;
  std::string input_file_name;
  std::string out_file_name;
  std::vector<std::string> geant_file_names;

  int data_low_x;
  int data_high_x;
  int peak_low_x;
  int peak_high_x;
  int fit_low_x;
  int fit_high_x;
  
  int kev_per_bin;
  int num_pars;
  int num_geant_files;

  //////////////////////////////////////////////////
  //Get configuration values from file
  isotope_name     = cfg->GetValue("ISOTOPE_NAME", "");
  if (isotope_name.empty()){
    std::cout << "ERROR: Failed to get ISOTOPE_NAME from cfg file: " << cfg_file_name << std::endl;
    return -1;
  }
  input_file_name     = cfg->GetValue("INPUT_FILE", "");
  if (input_file_name.empty()){
    std::cout << "ERROR: Failed to get INPUT_FILE from cfg file: " << cfg_file_name << std::endl;
    return -1;
  }

  out_file_name     = cfg->GetValue("OUT_FILE_NAME", "");
  if (out_file_name.empty()){
    std::cout << "ERROR: Failed to get OUT_FILE_NAME from cfg file: " << cfg_file_name << std::endl;
    return -1;
  }



  num_geant_files = cfg->GetValue("NUM_GEANT_FILES", 0);
  if (num_geant_files == 0){
    std::cout << "ERROR: Failed to get correct number of geant files from cfg file: " << cfg_file_name << std::endl;
    return -1;
  }

  for (int i = 0; i < num_geant_files; i++){
    std::string geant_file_name = cfg->GetValue(Form("GEANT_FILE_NAME_%d",i), "");
    if (geant_file_name.empty()){
      std::cout << "ERROR: Failed to get GEANT_FILE_NAME_"<<i<<" from cfg file: " << cfg_file_name << std::endl;
      return -1;
    }
    geant_file_names.push_back(geant_file_name);
  }
  
  data_low_x = cfg->GetValue("DATA_LOW_X", -1);
  if (data_low_x == -1){
    std::cout << "ERROR: Bad data_low_x Value : " << data_low_x << std::endl;
    return -1;
  }
  data_high_x = cfg->GetValue("DATA_HIGH_X", 0);
  if (data_high_x == 0){
    std::cout << "ERROR: Bad data_high_x Value : " << data_high_x << std::endl;
    return -1;
  }
  peak_low_x = cfg->GetValue("PEAK_LOW_X", -1);
  if (peak_low_x == -1){
    std::cout << "ERROR: Bad PEAK_LOW_X Value : " << peak_low_x << std::endl;
    return -1;
  }
  peak_high_x = cfg->GetValue("PEAK_HIGH_X", 0);
  if (peak_high_x == 0){
    std::cout << "ERROR: Bad PEAK_HIGH_X Value : " << peak_high_x << std::endl;
    return -1;
  }
  fit_low_x = cfg->GetValue("FIT_LOW_X", -1);
  if (fit_low_x == -1){
    std::cout << "ERROR: Bad fit_low_x Value : " << fit_low_x << std::endl;
    return -1;
  }
  fit_high_x = cfg->GetValue("FIT_HIGH_X", 0);
  if (fit_high_x == 0){
    std::cout << "ERROR: Bad fit_high_x Value : " << fit_high_x << std::endl;
    return -1;
  }
  kev_per_bin = cfg->GetValue("KEV_PER_BIN", 0);
  if (kev_per_bin == 0){
    std::cout << "ERROR: Bad kev_per_bin Value : " << kev_per_bin << std::endl;
    return -1;
  }

  num_pars = cfg->GetValue("NUM_PARS",0);
  if (num_pars == 0 || num_pars > MAX_PARS){
    std::cout << "ERROR: Bad NUM_PARS Value : " << num_pars << std::endl;
    return -1;
  }
  
  std::cout << "Using " << num_pars << " parameters: " << std::endl;
  for (int i = 0; i < num_pars; i++){
    init_pars[i] = cfg->GetValue(Form("PAR.%d", i), 0.0);
    std::cout << "Parameter " << i << " = " << init_pars[i] << std::endl;
    if (TMath::Abs(init_pars[i]) < 1e-08){
      std::cout << "ERROR: Failed to get parameter = " << i 
                << " from cfg file with num_pars =  " << num_pars << std::endl;
      return -1;
    }
  }

  //Finished parsing cfg file
  ////////////////////////////////////////////////////////////////////////////////////////////////////

  //Open file containing histograms 
  TFile *input_file = new TFile(input_file_name.c_str(), "read");
  if (!input_file){
    std::cout << "ERROR: Failed to open input file." << std::endl;
    return -1;
  }
  


  //Open Output file which will contain the histogram results
  out_file.open(out_file_name.c_str());
  if (!out_file){
    std::cout << "ERROR: Failed to open out_file!" << std::endl;
    return -1;
  }
  if (num_geant_files == 1){
    out_file << "HistConstant     FitError     ChiSquared     ResSum     ResSumInPeak     PeakSum" << std::endl;
  }
  else if (num_geant_files == 2){
    out_file << "HistConstant1     HistConstant2     FitError     ChiSquared     ResSum     ResSumInPeak     PeakSum" << std::endl;
  }
  

  ////////////////////////////////////////////////////////////////////////////////////////////////////
  //GET GEANT HISTOGRAMS

  getGeantHistograms(geant_hists,geant_file_names);
  if (geant_hists.size() != (unsigned int)num_geant_files*2){
    std::cout << "ERROR: Incorrect number of histograms ("<<geant_hists.size()
              <<") added to vector! Expected " << num_geant_files*2 <<  std::endl;
    return -1 ;
  }
  //Need to rebin these to match the binning from my filled histograms
  for (int geant_file = 0; geant_file < num_geant_files*2; geant_file+=2){
    geant_hists.at(geant_file)->GetXaxis()->SetRangeUser(data_low_x,data_high_x);
    geant_hists.at(geant_file)->Rebin(kev_per_bin);
    geant_hists.at(geant_file+1)->GetXaxis()->SetRangeUser(data_low_x,data_high_x);
    geant_hists.at(geant_file+1)->Rebin(kev_per_bin);
  }
  //DONE GETTING GEANT HISTOGRAMS
  ////////////////////////////////////////////////////////////////////////////////////////////////////

  getHistograms(data_hists, angles, input_file, START_ANGLE, FINAL_ANGLE,ANGLE_STEPS, 
                data_low_x, data_high_x, kev_per_bin);
  
  ////////////////////////////////////////////////////
  //Now Starting Fitting Routine
  int num_res_points = (fit_high_x - fit_low_x)/kev_per_bin;
  std::vector<double> residuals;
  std::vector<double> normed_residuals;
  std::vector<double> bin_centers;
  std::vector<double> peak_sum;
  residuals.resize(num_res_points);
  normed_residuals.resize(num_res_points);
  bin_centers.resize(num_res_points);
  peak_sum.resize(TOTAL_ANGLES);

  TF1 *used_fit_function = new TF1("double_expo_fit", 
                                   "[0]*([1]*TMath::Exp([2]*x)+[3]*TMath::Exp([4]*x))", data_low_x,data_high_x);
  if (num_geant_files == 1){
    used_fit_function->SetParameter(1,init_pars[2]);
    used_fit_function->SetParameter(2,init_pars[3]);
    used_fit_function->SetParameter(3,init_pars[4]);
    used_fit_function->SetParameter(4,init_pars[5]);
  }
  else if (num_geant_files == 2){
    used_fit_function->SetParameter(1,init_pars[3]);
    used_fit_function->SetParameter(2,init_pars[4]);
    used_fit_function->SetParameter(3,init_pars[5]);
    used_fit_function->SetParameter(4,init_pars[6]);
  }

  TFile *out_hist_file = new TFile("output_fit_hists.root","recreate");
  for (int angle_index = 0; angle_index < TOTAL_ANGLES; angle_index++){
    peak_sum[angle_index] = 0;
    TF1 *fit_func = 0;
  
    if (num_geant_files == 1){
      fit_func = FitDoubleExpHist(data_hists.at(angle_index), geant_hists.at(0), fit_low_x, fit_high_x, init_pars);
    }
    else if (num_geant_files == 2){
      fit_func = FitDoubleExpTwoHist(data_hists.at(angle_index), geant_hists.at(0), geant_hists.at(2), fit_low_x, fit_high_x, init_pars);
    }
    data_hists.at(angle_index)->SetTitle(Form("Fit to %s with Scatter Cut (< %3.3f)", isotope_name.c_str(),angles.at(angle_index)));
    data_hists.at(angle_index)->GetXaxis()->SetTitle("Energy (keV)");
    data_hists.at(angle_index)->GetYaxis()->SetTitle(Form("Counts / %d keV", kev_per_bin));

    for (int i = 0; i < num_geant_files; i++){
      hist_constant[angle_index][i] = fit_func->GetParameter(i);
      std::cout << "i = " << i << " angle_index = " << angle_index 
                << " hist_constant[angle_index][i] = "  
                << hist_constant[angle_index][i] << std::endl;
    }

    fit_error[angle_index]     = fit_func->GetParError(0); 
    chi_squared[angle_index]   = fit_func->GetChisquare();

    //Now we get residuals
    double res_sum = 0;
    double res_sum_in_peak = 0;

    int start_bin = data_hists[angle_index]->FindBin(fit_low_x);
    int end_bin = data_hists[angle_index]->FindBin(fit_high_x);
    if (angle_index == 0){
      if (num_geant_files == 1){
        used_fit_function->SetParameter(0, fit_func->GetParameter(1));//set double exponential scaling factor
      }
      else if (num_geant_files == 2){
        used_fit_function->SetParameter(0, fit_func->GetParameter(2));//set double exponential scaling factor
      }
    }
    
    for (int bin = start_bin; bin < end_bin; bin++){
      bin_centers[bin-start_bin] = data_hists.at(angle_index)->GetBinCenter(bin);
      residuals[bin-start_bin]   = data_hists.at(angle_index)->GetBinContent(bin) - 
                                   fit_func->Eval(bin_centers[bin-start_bin]);
      res_sum += residuals[bin-start_bin];
      normed_residuals[bin-start_bin] = residuals[bin-start_bin]/TMath::Sqrt(data_hists[angle_index]->GetBinContent(bin));

      if (bin_centers[bin-start_bin] >= peak_low_x  && bin_centers[bin-start_bin] <= peak_high_x){
        if (num_geant_files == 1){
          peak_sum[angle_index] += (fit_func->Eval(bin_centers[bin-start_bin])-used_fit_function->Eval(bin_centers[bin-start_bin]));
        }
        if (num_geant_files == 2){
          TH1D geant_compton_hist = *((TH1D*)geant_hists.at(3)->Clone("geant_compton_hist"));
          geant_compton_hist.Scale(hist_constant[angle_index][1]);
          peak_sum[angle_index] += (fit_func->Eval(bin_centers[bin-start_bin])-used_fit_function->Eval(bin_centers[bin-start_bin]) - geant_compton_hist.GetBinContent(bin));
        }
        res_sum_in_peak += residuals[bin-start_bin];
      }
    }//bin loop


    TGraph *residual_plot = new TGraph(num_res_points, &bin_centers[0], &residuals[0]);
    residual_plot->SetName(Form("res_plot_angle_%f", angles.at(angle_index)));
    TGraph *norm_residual_plot = new TGraph(num_res_points, &bin_centers[0], &normed_residuals[0]);
    norm_residual_plot->SetName(Form("norm_res_plot_angle_%f", angles.at(angle_index)));

    if (num_geant_files == 1){
      out_file <<  hist_constant[angle_index][0] << "     " << fit_error[angle_index] << "     " << chi_squared[angle_index] << "     " << res_sum << "     " << res_sum_in_peak << "     " << peak_sum[angle_index] << "     " << std::endl;
    }

    else if (num_geant_files == 2){
      out_file <<  hist_constant[angle_index][0] << "     " << "     " << hist_constant[angle_index][1] << "     " << fit_error[angle_index] << "     " << chi_squared[angle_index] << "     " << res_sum << "     " << res_sum_in_peak << "     " << peak_sum[angle_index] << "     " << std::endl;
    }

        

    out_hist_file->cd();
    residual_plot->Write();
    norm_residual_plot->Write();
    data_hists[angle_index]->Write();


    delete residual_plot;
    delete norm_residual_plot;
    

    delete fit_func;
    if (angle_index % 10 == 0){
      std::cout << " Completed Angle " << angles[angle_index] << std::endl; 
    }
  }

  TCanvas *disentangled_can = new TCanvas("dis_can","dis_can", 800,600);
  disentangled_can->cd();
  data_hists[0]->Draw();
  TLine *fit_low = new TLine(fit_low_x, 0, fit_low_x, 780);
  TLine *fit_high = new TLine(fit_high_x, 0, fit_high_x, 780);
  TLine *peak_low = new TLine(peak_low_x, 0, peak_low_x, 780);
  TLine *peak_high = new TLine(peak_high_x, 0, peak_high_x, 780);
  fit_low->SetLineStyle(2);
  fit_high->SetLineStyle(2);
  peak_low->SetLineStyle(3);
  peak_high->SetLineStyle(3);
  fit_low->Draw();
  fit_high->Draw();
  peak_low->Draw();
  peak_high->Draw();

  geant_hists.at(1)->SetLineColor(kRed);
  used_fit_function->SetLineColor(kBlack);
  //FOR DEBUGGING
  geant_hists.at(1)->Scale(hist_constant[0][0]);
  geant_hists.at(1)->Draw("same");
  if (num_geant_files == 2){
    geant_hists.at(3)->Scale(hist_constant[0][1]);
    geant_hists.at(3)->Draw("same");
  }
  if (!used_fit_function){
    std::cout << "Fit function doesn't exist." << std::endl;
  }
  else{
    used_fit_function->Draw("same");
  }
  
  out_hist_file->cd();
  disentangled_can->Write();
  out_file.close();
  //END OF OLD FILE
  ////////////////////////////////////////////////////////////////////////////////////////////////////
  return 0;
}//End of Function

#ifndef __CINT__
int main(int argc, char **argv){
  if (argc != 2){
    std::cout << "Error! Incorrect number of arguments: " << argc << std::endl;
    std::cout << "Usage: fitCoulex CFG_FILE";
    return 1;
  }

  fitCoulex(argv[1]);
}
#endif
