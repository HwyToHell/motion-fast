#ifndef BACKGROUNDSUBTRACTION_H
#define BACKGROUNDSUBTRACTION_H
#include <opencv2/opencv.hpp>

/// background subtractor: first order low pass filter
class BackgroundSubtractorLowPass : public cv::BackgroundSubtractor {
public:
	BackgroundSubtractorLowPass(double alpha, double threshold);
	~BackgroundSubtractorLowPass();
	virtual void apply(cv::InputArray image, cv::OutputArray fgmask, double learningRate=-1);
	virtual void getBackgroundImage(cv::OutputArray backgroundImage) const;
    double       threshold() const;
    void         threshold(double threshold);
private:
	cv::Mat	m_accu;
	double	m_alpha;
	bool	m_isInitialized;
	double	m_threshold;
};

cv::Ptr<BackgroundSubtractorLowPass> createBackgroundSubtractorLowPass(double alpha, double threshold);

#endif // BACKGROUNDSUBTRACTION_H
