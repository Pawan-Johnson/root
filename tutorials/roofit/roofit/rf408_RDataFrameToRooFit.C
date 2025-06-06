/// \file
/// \ingroup tutorial_roofit_main
/// \notebook
/// Fill RooDataSet/RooDataHist in RDataFrame.
///
/// This tutorial shows how to fill RooFit data classes directly from RDataFrame.
/// Using two small helpers, we tell RDataFrame where the data has to go.
///
/// \macro_code
/// \macro_output
///
/// \date Mar 2021
/// \author Stephan Hageboeck (CERN)

#include <RooAbsDataHelper.h>

#include <TRandom.h>

/// Print the first few entries and summary statistics.
void printData(const RooAbsData& data) {
  std::cout << "\n";
  data.Print();

  for (int i=0; i < data.numEntries() && i < 20; ++i) {
    std::cout << "(";
    for (const auto var : *data.get(i)) {
      std::cout << std::setprecision(3) << std::right << std::fixed << std::setw(8) << static_cast<const RooAbsReal*>(var)->getVal() << ", ";
    }
    std::cout << ")\tweight=" << std::setw(10) << data.weight() << std::endl;
  }

  // Get the x and y variables from the dataset:
  const auto & x = static_cast<const RooRealVar&>(*(*data.get())[0]);
  const auto & y = static_cast<const RooRealVar&>(*(*data.get())[1]);

  std::cout << "mean(x) = " << data.mean(x) << "\tsigma(x) = " << std::sqrt(data.moment(x, 2.))
    << "\n" << "mean(y) = " << data.mean(y) << "\tsigma(y) = " << std::sqrt(data.moment(y, 2.)) << std::endl;
}

void rf408_RDataFrameToRooFit()
{
  // Set up
  // ------------------------

  // We create an RDataFrame with two columns filled with 2 million random numbers.
  auto df = ROOT::RDataFrame{2000000}.Define("x", []() { return gRandom->Uniform(-5., 5.); }).Define("y", []() {
     return gRandom->Gaus(1., 3.);
  });


  // We create RooFit variables that will represent the dataset.
  RooRealVar x("x", "x", -5.,   5.);
  RooRealVar y("y", "y", -50., 50.);
  x.setBins(10);
  y.setBins(20);



  // Booking the creation of RooDataSet / RooDataHist in RDataFrame
  // ----------------------------------------------------------------

  // Method 1:
  // ---------
  // We directly book the RooDataSetHelper action.
  // We need to pass
  // - the RDataFrame column types as template parameters
  // - the constructor arguments for RooDataSet (they follow the same syntax as the usual RooDataSet constructors)
  // - the column names that RDataFrame should fill into the dataset
  //
  // NOTE: RDataFrame columns are matched to RooFit variables by position, *not by name*!
  //
  // The returned object is not yet a RooDataSet, but an RResultPtr that will
  // be lazy-evaluated once you call GetValue() on it. We will only evaluate
  // the RResultPtr once all other RDataFrame related actions are declared.
  // This way we trigger the event loop computation only once, which will
  // improve the runtime significantly.
  //
  // To learn more about lazy actions, see:
  //     https://root.cern/doc/master/classROOT_1_1RDataFrame.html#actions
  ROOT::RDF::RResultPtr<RooDataSet> rooDataSetResult = df.Book<double, double>(
      RooDataSetHelper("dataset", // Name
          "Title of dataset",     // Title
          RooArgSet(x, y)         // Variables in this dataset
          ),
      {"x", "y"}                  // Column names in RDataFrame.
  );


  // Method 2:
  // ---------
  // We first declare the RooDataHistHelper
  RooDataHistHelper rdhMaker{"datahist",  // Name
    "Title of data hist",                 // Title
    RooArgSet(x, y)                       // Variables in this dataset
  };

  // Then, we move it into an RDataFrame action:
  ROOT::RDF::RResultPtr<RooDataHist> rooDataHistResult = df.Book<double, double>(std::move(rdhMaker), {"x", "y"});


  // Run it and inspect the results
  // -------------------------------

  // At this point, all RDF actions were defined (namely, the `Book`
  // operations), so we can get values from the RResultPtr objects, triggering
  // the event loop and getting the actual RooFit data objects.
  RooDataSet const& rooDataSet = rooDataSetResult.GetValue();
  RooDataHist const& rooDataHist = rooDataHistResult.GetValue();

  // Let's inspect the dataset / datahist.
  printData(rooDataSet);
  printData(rooDataHist);
}

int main() {
  rf408_RDataFrameToRooFit();
  return 0;
}
