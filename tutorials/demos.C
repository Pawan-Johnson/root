/// \file
/// \ingroup Tutorials
/// This macro shows a control bar to run some of the ROOT tutorials.
/// To execute an item, click with the left mouse button.
///
/// \macro_code
///
/// \author Rene Brun

void demos() {

   //Add the tutorials directory to the macro path
   //This is necessary in case this macro is executed from another user directory
   TString dirName = gSystem->GetDirName(gInterpreter->GetCurrentMacroName());
   const char *current = gROOT->GetMacroPath();
   gROOT->SetMacroPath(TString::Format("%s:%s", current, dirName.Data()));

   TControlBar *bar = new TControlBar("vertical", "Demos", 10, 10);
   bar->AddButton("Help Demos",    ".x demoshelp.C",          "Click Here For Help on Running the Demos");
   bar->AddButton("browser",       "new TBrowser;",           "Start the ROOT Browser");
   bar->AddButton("framework",     ".x visualisation/graphics/framework.C", "An Example of Object Oriented User Interface");
   bar->AddButton("first",         ".x visualisation/graphics/first.C",     "An Example of Slide with Root");
   bar->AddButton("hsimple",       ".x hsimple.C",            "An Example Creating Histograms/Ntuples on File");
   bar->AddButton("hsum",          ".x hist/hist007_TH1_liveupdate.C",  "Filling and live update of histogram");
   bar->AddButton("formula1",      ".x visualisation/graphics/formula1.C",  "Simple Formula and Functions");
   bar->AddButton("surfaces",      ".x visualisation/graphics/surfaces.C",    "Surface Drawing Options");
   bar->AddButton("fillrandom",    ".x hist/hist001_TH1_fillrandom.C",   "Histograms with Random Numbers from a Function");
   bar->AddButton("fit1",          ".x math/fit/fit1.C", "A Simple Fitting Example");
   bar->AddButton("multifit",      ".x math/fit/multifit.C", "Fitting in Subranges of Histograms");
   bar->AddButton("h1ReadAndDraw", ".x hist/hist015_TH1_read_and_draw.C", "Drawing Options for 1D Histograms");
   bar->AddButton("graph",         ".x visualisation/graphs/gr001_simple.C",  "Example of a Simple Graph");
   bar->AddButton("gerrors",       ".x visualisation/graphs/gr002_errors.C",  "Example of a Graph with Error Bars");
   bar->AddButton("tornado",       ".x visualisation/graphics/tornado.C",   "Examples of 3-D PolyMarkers");
   bar->AddButton("geometry",      ".x visualisation/geom/rootgeom.C",      "Example of TGeoManager drawing");
   bar->AddButton("file",          ".x io/file.C",            "The ROOT File Format");
   bar->AddButton("fildir",        ".x io/fildir.C",          "The ROOT File, Directories and Keys");
   bar->AddButton("tree",          ".x legacy/tree/tree.C",   "The Tree Data Structure");
   bar->AddButton("ntuple1",       ".x io/tree/tree120_ntuple.C", "Ntuples and Selections");
   bar->AddButton("benchmarks",    ".x legacy/benchmarks.C",  "Runs several tests and produces an benchmark report");
   bar->AddButton("rootmarks",     ".x legacy/rootmarks.C",   "Prints an Estimated ROOTMARKS for Your Machine");
   bar->SetButtonWidth(90);
   bar->Show();
   gROOT->SaveContext();
}
