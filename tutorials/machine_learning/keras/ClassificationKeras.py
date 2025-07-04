## \file
## \ingroup tutorial_tmva_keras
## \notebook -nodraw
## This tutorial shows how to do classification in TMVA with neural networks
## trained with keras.
##
## \macro_code
##
## \date 2017
## \author TMVA Team

from ROOT import TMVA, TFile, TCut, gROOT
from subprocess import call
from os.path import isfile

from tensorflow.keras.models import Sequential
from tensorflow.keras.layers import Dense
from tensorflow.keras.optimizers import SGD


def create_model():
    # Generate model

    # Define model
    model = Sequential()
    model.add(Dense(64, activation='relu', input_dim=4))
    model.add(Dense(2, activation='softmax'))

    # Set loss and optimizer
    model.compile(loss='categorical_crossentropy',
                  optimizer=SGD(learning_rate=0.01), weighted_metrics=['accuracy', ])

    # Store model to file
    model.save('modelClassification.h5')
    model.summary()


def run():
    with TFile.Open('TMVA_Classification_Keras.root', 'RECREATE') as output, TFile.Open(str(gROOT.GetTutorialDir()) + '/machine_learning/data/tmva_class_example.root') as data:
        factory = TMVA.Factory('TMVAClassification', output,
                               '!V:!Silent:Color:DrawProgressBar:Transformations=D,G:AnalysisType=Classification')

        signal = data.Get('TreeS')
        background = data.Get('TreeB')

        dataloader = TMVA.DataLoader('dataset')
        for branch in signal.GetListOfBranches():
            dataloader.AddVariable(branch.GetName())

        dataloader.AddSignalTree(signal, 1.0)
        dataloader.AddBackgroundTree(background, 1.0)
        dataloader.PrepareTrainingAndTestTree(TCut(''),
                                              'nTrain_Signal=4000:nTrain_Background=4000:SplitMode=Random:NormMode=NumEvents:!V')

        # Book methods
        factory.BookMethod(dataloader, TMVA.Types.kFisher, 'Fisher',
                           '!H:!V:Fisher:VarTransform=D,G')
        factory.BookMethod(dataloader, TMVA.Types.kPyKeras, 'PyKeras',
                           'H:!V:VarTransform=D,G:FilenameModel=modelClassification.h5:FilenameTrainedModel=trainedModelClassification.h5:NumEpochs=20:BatchSize=32:LearningRateSchedule=10,0.01;20,0.005')

        # Run training, test and evaluation
        factory.TrainAllMethods()
        factory.TestAllMethods()
        factory.EvaluateAllMethods()


if __name__ == "__main__":
    # Setup TMVA
    TMVA.Tools.Instance()
    TMVA.PyMethodBase.PyInitialize()

    # Create and store the ML model
    create_model()

    # Run TMVA
    run()
