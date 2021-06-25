#include "backgroundsubtraction.h"


/// background subtractor: first order low pass filter
cv::Ptr<BackgroundSubtractorLowPass> createBackgroundSubtractorLowPass(double alpha, double threshold) {
    return cv::makePtr<BackgroundSubtractorLowPass>(alpha, threshold);
}


BackgroundSubtractorLowPass::BackgroundSubtractorLowPass(double alpha, double threshold) : 
	m_alpha(alpha), 
	m_isInitialized(false),
    m_threshold(threshold)
{
}


BackgroundSubtractorLowPass::~BackgroundSubtractorLowPass()
{
}


void BackgroundSubtractorLowPass::apply(cv::InputArray image, cv::OutputArray fgmask, double learningRate)
{
    std::ignore = learningRate;
	// fill accu when applying for first time
	if (!m_isInitialized) {
		m_accu = cv::Mat(image.size(), CV_32F);
		image.getMat().convertTo(m_accu, CV_32F);
		fgmask.assign(cv::Mat(image.size(), CV_8UC1));
		m_isInitialized = true;
	// actual segmentation algorithm
	} else { 
		cv::accumulateWeighted(image, m_accu, m_alpha);
		cv::Mat accu8U;
		cv::convertScaleAbs(m_accu, accu8U);
		cv::absdiff(image, accu8U, fgmask);
		cv::cvtColor(fgmask, fgmask, cv::COLOR_BGR2GRAY);
        cv::threshold(fgmask, fgmask, m_threshold, UCHAR_MAX, cv::THRESH_BINARY);
	}

	return;
}


void BackgroundSubtractorLowPass::getBackgroundImage(cv::OutputArray backgroundImage) const
{
	m_accu.convertTo(backgroundImage, CV_8U);
	return;
}


double BackgroundSubtractorLowPass::threshold() const
{
    return m_threshold;
}


void BackgroundSubtractorLowPass::threshold(double threshold)
{
    m_threshold = threshold;
}
