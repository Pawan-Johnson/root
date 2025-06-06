/// \file
/// \ingroup tutorial_tree
/// \notebook
/// Example of a Tree where branches are variable length arrays
/// A second Tree is created and filled in parallel.
/// Run this script with
/// ~~~
///   .x tree107_tree.C
/// ~~~
/// In the function treer, the first Tree is open.
/// The second Tree is declared friend of the first tree.
/// TTree::Draw is called with variables from both Trees.
/// \macro_code
///
/// \author Rene Brun

#include "TFile.h"
#include "TTree.h"
#include "TRandom.h"
#include "TCanvas.h"

void tree107_write()
{
   const Int_t kMaxTrack = 500;
   Int_t ntrack;
   Int_t stat[kMaxTrack];
   Int_t sign[kMaxTrack];
   Float_t px[kMaxTrack];
   Float_t py[kMaxTrack];
   Float_t pz[kMaxTrack];
   Float_t pt[kMaxTrack];
   Float_t zv[kMaxTrack];
   Float_t chi2[kMaxTrack];
   Double_t sumstat;

   TFile f("tree108.root", "recreate");
   auto t3 = new TTree("t3", "Reconst ntuple");
   t3->Branch("ntrack", &ntrack, "ntrack/I");
   t3->Branch("stat", stat, "stat[ntrack]/I");
   t3->Branch("sign", sign, "sign[ntrack]/I");
   t3->Branch("px", px, "px[ntrack]/F");
   t3->Branch("py", py, "py[ntrack]/F");
   t3->Branch("pz", pz, "pz[ntrack]/F");
   t3->Branch("zv", zv, "zv[ntrack]/F");
   t3->Branch("chi2", chi2, "chi2[ntrack]/F");

   TFile fr("tree108f.root", "recreate");
   auto t3f = new TTree("t3f", "a friend Tree");
   t3f->Branch("ntrack", &ntrack, "ntrack/I");
   t3f->Branch("sumstat", &sumstat, "sumstat/D");
   t3f->Branch("pt", pt, "pt[ntrack]/F");

   for (Int_t i=0; i<1000; i++) {
      Int_t nt = gRandom->Rndm() * (kMaxTrack - 1);
      ntrack = nt;
      sumstat = 0;
      for (Int_t n=0; n<nt; n++) {
         stat[n] = n % 3;
         sign[n] = i % 2;
         px[n]   = gRandom->Gaus(0, 1);
         py[n]   = gRandom->Gaus(0, 2);
         pz[n]   = gRandom->Gaus(10, 5);
         zv[n]   = gRandom->Gaus(100, 2);
         chi2[n] = gRandom->Gaus(0, .01);
         sumstat += chi2[n];
         pt[n]   = TMath::Sqrt(px[n] * px[n] + py[n] * py[n]);
      }
      t3->Fill();
      t3f->Fill();
   }
   t3->Print();
   f.cd();
   t3->Write();
   fr.cd();
   t3f->Write();
}

void tree107_read()
{
   auto f = TFile::Open("tree108.root");
   auto t3 = f->Get<TTree>("t3");
   t3->AddFriend("t3f", "tree108f.root");
   t3->Draw("pz", "pt>3");
}

void tree107_read2()
{
   auto p = new TPad("p", "p", 0.6, 0.4, 0.98, 0.8);
   p->Draw();
   p->cd();
   auto f1 = TFile::Open("tree108.root");
   auto f2 = TFile::Open("tree108f.root");
   auto t3 = f1->Get<TTree>("t3");
   t3->AddFriend("t3f", f2);
   t3->Draw("pz", "pt>3");
}

void tree107_tree()
{
   tree107_write();
   tree107_read();
   tree107_read2();
}
