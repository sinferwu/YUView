/*  This file is part of YUView - The YUV player with advanced analytics toolset
*   <https://github.com/IENT/YUView>
*   Copyright (C) 2015  Institut f�r Nachrichtentechnik, RWTH Aachen University, GERMANY
*
*   This program is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 3 of the License, or
*   (at your option) any later version.
*
*   In addition, as a special exception, the copyright holders give
*   permission to link the code of portions of this program with the
*   OpenSSL library under certain conditions as described in each
*   individual source file, and distribute linked combinations including
*   the two.
*   
*   You must obey the GNU General Public License in all respects for all
*   of the code used other than OpenSSL. If you modify file(s) with this
*   exception, you may extend this exception to your version of the
*   file(s), but you are not obligated to do so. If you do not wish to do
*   so, delete this exception statement from your version. If you delete
*   this exception statement from all source files in the program, then
*   also delete it here.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "bitstreamAnalysisWidget.h"

#include "parserAnnexBAVC.h"
#include "parserAnnexBHEVC.h"
#include "parserAnnexBMpeg2.h"
#include "parserAVFormat.h"

#define BITSTREAM_ANALYSIS_WIDGET_DEBUG_OUTPUT 1
#if BITSTREAM_ANALYSIS_WIDGET_DEBUG_OUTPUT
#include <QDebug>
#define DEBUG_ANALYSIS qDebug
#else
#define DEBUG_ANALYSIS(fmt,...) ((void)0)
#endif

BitstreamAnalysisWidget::BitstreamAnalysisWidget(QWidget *parent) :
  QWidget(parent)
{
  ui.setupUi(this);

  ui.streamInfoTreeWidget->setColumnWidth(0, 300);

  updateParsingStatusText(-1);

  connect(ui.showVideoStreamOnlyCheckBox, &QCheckBox::toggled, this, &BitstreamAnalysisWidget::showVideoStreamOnlyCheckBoxToggled);
  connect(ui.colorCodeStreamsCheckBox, &QCheckBox::toggled, this, &BitstreamAnalysisWidget::colorCodeStreamsCheckBoxToggled);
  connect(ui.parseEntireFileCheckBox, &QCheckBox::toggled, this, &BitstreamAnalysisWidget::parseEntireBitstreamCheckBoxToggled);

  ui.bitrateGraphicsView->chart()->setAnimationOptions(QChart::AllAnimations);
  ui.bitrateGraphicsView->chart()->setTitle("Bitrate over time");

  currentSelectedItemsChanged(nullptr, nullptr, false);
}

void BitstreamAnalysisWidget::updateParserItemModel(unsigned int newNumberItems)
{
  parser->setNewNumberModelItems(newNumberItems);
  updateParsingStatusText(parser->getParsingProgressPercent());
}

void BitstreamAnalysisWidget::updateStreamInfo()
{
  ui.streamInfoTreeWidget->clear();
  ui.streamInfoTreeWidget->addTopLevelItems(parser->getStreamInfo());
  ui.streamInfoTreeWidget->expandAll();
  ui.showVideoStreamOnlyCheckBox->setEnabled(parser->getNrStreams() > 1);
}

void BitstreamAnalysisWidget::backgroundParsingDone(QString error)
{
  if (error.isEmpty())
    ui.parsingStatusText->setText("Parsing done.");
  else
    ui.parsingStatusText->setText("Error parsing the file: " + error);
  updateParsingStatusText(100);
}

void BitstreamAnalysisWidget::showVideoStreamOnlyCheckBoxToggled(bool state)
{
  if (!parser)
    return;

  if (showVideoStreamOnly != state)
  {
    showVideoStreamOnly = state;
    if (showVideoStreamOnly)
      parser->setFilterStreamIndex(parser->getVideoStreamIndex());
    else
      parser->setFilterStreamIndex(-1);
  }
}

void BitstreamAnalysisWidget::updateParsingStatusText(int progressValue)
{
  if (progressValue <= -1)
    ui.parsingStatusText->setText("No bitstream file selected - Select a bitstream file to start bitstream analysis.");
  else if (progressValue < 100)
    ui.parsingStatusText->setText(QString("Parsing file (%1%)").arg(progressValue));
  else
  {
    const bool parsingLimitSet = !ui.parseEntireFileCheckBox->isChecked();
    ui.parsingStatusText->setText(parsingLimitSet ? "Partial parsing done. Enable full parsing if needed." : "Parsing done.");
  }
}

void BitstreamAnalysisWidget::stopAndDeleteParser()
{
  if (backgroundParserFuture.isRunning())
  {
    DEBUG_ANALYSIS("BitstreamAnalysisWidget::stopAndDeleteParser stopping parser");
    parser->setAbortParsing();
    backgroundParserFuture.waitForFinished();
  }
  parser.reset();
  DEBUG_ANALYSIS("BitstreamAnalysisWidget::stopAndDeleteParser parser stopped and deleted");
}

void BitstreamAnalysisWidget::backgroundParsingFunction()
{
  if (parser)
    parser->runParsingOfFile(compressedFilePath);
}

void BitstreamAnalysisWidget::currentSelectedItemsChanged(playlistItem *item1, playlistItem *item2, bool chageByPlayback)
{
  Q_UNUSED(item2);
  Q_UNUSED(chageByPlayback);

  currentCompressedVideo = dynamic_cast<playlistItemCompressedVideo*>(item1);
  ui.streamInfoTreeWidget->clear();

  const bool isBitstream = !currentCompressedVideo.isNull();
  ui.tabStreamInfo->setEnabled(isBitstream);
  ui.tabPacketAnalysis->setEnabled(isBitstream);

  restartParsingOfCurrentItem();
}

void BitstreamAnalysisWidget::restartParsingOfCurrentItem()
{
  if (!isVisible())
  {
    DEBUG_ANALYSIS("BitstreamAnalysisWidget::restartParsingOfCurrentItem not visible - abort");
    return;
  }

  stopAndDeleteParser();
  
  if (currentCompressedVideo.isNull())
  {
    DEBUG_ANALYSIS("BitstreamAnalysisWidget::restartParsingOfCurrentItem no compressed video - abort");
    updateParsingStatusText(-1);
    parser.reset();
    return;
  }

  // Setup a new parser
  inputFormat inputFormatType = currentCompressedVideo->getInputFormat();
  if (inputFormatType == inputAnnexBHEVC)
    parser.reset(new parserAnnexBHEVC(this));
  else if (inputFormatType == inputAnnexBAVC)
    parser.reset(new parserAnnexBAVC(this));
  else if (inputFormatType == inputLibavformat)
    parser.reset(new parserAVFormat(this));
  parser->enableModel();
  const bool parsingLimitSet = !ui.parseEntireFileCheckBox->isChecked();
  parser->setParsingLimitEnabled(parsingLimitSet);

  connect(parser.data(), &parserBase::nalModelUpdated, this, &BitstreamAnalysisWidget::updateParserItemModel);
  //connect(parser.data(), &parserBase::bitrateDataUpdated, this, &BitstreamAnalysisWidget::updateBitrateDisplay);
  connect(parser.data(), &parserBase::streamInfoUpdated, this, &BitstreamAnalysisWidget::updateStreamInfo);
  connect(parser.data(), &parserBase::backgroundParsingDone, this, &BitstreamAnalysisWidget::backgroundParsingDone);

  compressedFilePath = currentCompressedVideo->getName();

  ui.dataTreeView->setModel(parser->getPacketItemModel());
  ui.dataTreeView->setColumnWidth(0, 600);
  ui.dataTreeView->setColumnWidth(1, 100);
  ui.dataTreeView->setColumnWidth(2, 120);

  updateStreamInfo();

  // Test: Set the model to show in the chart view
  QLineSeries *series = new QLineSeries;
  series->setName("Line 1 Test");
  QVXYModelMapper *mapper = new QVXYModelMapper(this);
  mapper->setXColumn(0);
  mapper->setYColumn(1);
  mapper->setSeries(series);
  mapper->setModel(parser->getBitrateItemModel());
  ui.bitrateGraphicsView->chart()->addSeries(series);
  ui.bitrateGraphicsView->chart()->createDefaultAxes();
  
  // Start the background parsing thread
  updateParsingStatusText(0);
  backgroundParserFuture = QtConcurrent::run(this, &BitstreamAnalysisWidget::backgroundParsingFunction);
  DEBUG_ANALYSIS("BitstreamAnalysisWidget::restartParsingOfCurrentItem new parser created and started");
}

void BitstreamAnalysisWidget::hideEvent(QHideEvent *event)
{
  DEBUG_ANALYSIS("BitstreamAnalysisWidget::hideEvent");
  stopAndDeleteParser();
  QWidget::hideEvent(event);
}

void BitstreamAnalysisWidget::showEvent(QShowEvent *event)
{
  DEBUG_ANALYSIS("BitstreamAnalysisWidget::showEvent");
  restartParsingOfCurrentItem();
  QWidget::showEvent(event);
}

// void BitstreamAnalysisWidget::updateBitrateDisplay()
// {
//   if (parser->getNrStreams() == 0)
//     return;

//   // Add the new data
//   QList<QAbstractSeries*> seriesList = ui.bitrateGraphicsView->chart()->series();
//   if (seriesList.length() == 0)
//   {
//     // Add one bar series (one stream at a time)
//     QBarSeries* series = new QBarSeries(ui.bitrateGraphicsView);
//     series->setName(QString("bitrate TEST"));
//     ui.bitrateGraphicsView->chart()->addSeries(series);
    
//     seriesList = ui.bitrateGraphicsView->chart()->series();
//     ui.bitrateGraphicsView->chart()->createDefaultAxes();
//   }

//   auto bitrateData = parser->getbitrateData();

//   unsigned int xMax = 0;
//   unsigned int yMax = 0;

//   // TODO: How do we plot multiple streams at once?
//   //       I think we need a selector to select individual streams as plotting multiple does not make sense
//   for (unsigned int streamIdx = 0; streamIdx < 1; streamIdx++)
//   {
//     QAbstractSeries *series = seriesList[0];
//     QBarSeries *barSeries = dynamic_cast<QBarSeries*>(series);
//     if (!barSeries)
//       return;

//     auto streamData = bitrateData[streamIdx];

//     auto it = streamData.constBegin();
//     unsigned int i = 0;
//     QBarSet *set = new QBarSet("Bitrate test");
//     while (it != streamData.constEnd())
//     {
//       set->append(it.value());
//       if (it.value() > yMax)
//         yMax = it.value();

//     //  if (!axisRangeInitialized)
//     //  {
//     //    rangeAxisX.first = s.startTime;
//     //    rangeAxisX.second = s.endTime;
//     //    rangeAxisY.second = s.bytes;
//     //  }

//     //  if (s.startTime < rangeAxisX.first)
//     //    rangeAxisX.first = s.startTime;
//     //  if (s.endTime > rangeAxisX.second)
//     //    rangeAxisX.second = s.endTime;
//     //  if ((qint64)s.bytes > rangeAxisY.second)
//     //    rangeAxisY.second = s.bytes;

//     //  axisRangeInitialized = true;

//       DEBUG_ANALYSIS("bitstreamAnalysisDialog::updateBitrateDisplay add bar at %d %d", it.key(), (unsigned int)it.value());
//       it++;
//       i++;
//     //  addedSegmentsPerStream[streamIdx]++;
//     }

//     xMax = i;

//     DEBUG_ANALYSIS("bitstreamAnalysisDialog::updateBitrateDisplay Add sets");
//     barSeries->append(set);
//     DEBUG_ANALYSIS("bitstreamAnalysisDialog::updateBitrateDisplay Add sets - Done");
//   }

//   ui.bitrateGraphicsView->chart()->axisX()->setRange(0, xMax);
//   ui.bitrateGraphicsView->chart()->axisY()->setRange(0, yMax);
//   DEBUG_ANALYSIS("bitstreamAnalysisDialog::updateBitrateDisplay Chart max values %d %d", xMax, yMax);

//   //DEBUG_ANALYSIS("bitstreamAnalysisDialog::updateBitrateDisplay axis limits (%d, %d) (%d, %d)", rangeAxisX.first, rangeAxisX.second, rangeAxisY.first, rangeAxisY.second);
//   //DEBUG_ANALYSIS("bitstreamAnalysisDialog::updateBitrateDisplay new segment count %d", barSeries->count());
// }