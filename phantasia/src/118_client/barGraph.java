import java.awt.*;

public class barGraph extends Canvas implements constants {

    float percent = 0;
    float otherPercent = 0;
    long otherMax = 0;
    String theValue;
    int cx = 0;
    int canvasWidth = 0;
    int canvasHeight = 0;
    int theFill = 0;
    Rectangle theBounds = null;
    Color fillColor = null;
    FontMetrics fm = null;
    Font theFont = new Font("Helvetica", Font.BOLD, 12);

    public barGraph() {

	super();
	setBackground(highlightColor);
	percent = 0;
	theValue = "0";
    }

    public void paint(Graphics g) {

	if (g == null) {
	    return;
	}

	Graphics2D g2 = Theme.smooth(g);

	theBounds = getBounds();
	canvasWidth = theBounds.width - 1;
	canvasHeight = theBounds.height - 1;

	    /* red when low, gold in the middle, green when full */
	if (percent >= 1) {
	    fillColor = new Color(95, 163, 90);
	}
	else if (percent < .25) {
	    fillColor = new Color(196, 72, 62);
	}
	else if (percent < .6) {
	    fillColor = new Color(214, 140, 60);
	}
	else {
	    fillColor = new Color(214, 168, 74);
	}

	g2.setColor(highlightColor);
	g2.fillRect(0, 0, canvasWidth + 1, canvasHeight + 1);

	    /* track */
	int barHeight = Math.max(8, canvasHeight - 6);
	int top = (canvasHeight - barHeight) / 2;
	g2.setColor(fieldColor);
	g2.fillRoundRect(0, top, canvasWidth, barHeight, barHeight, barHeight);
	g2.setColor(lineColor);
	g2.drawRoundRect(0, top, canvasWidth, barHeight, barHeight, barHeight);

	    /* fill */
	theFill = (int) (Math.max(0, Math.min(1, percent)) * (canvasWidth - 2));
	if (theFill > 0) {
	    g2.setColor(fillColor);
	    g2.fillRoundRect(1, top + 1, theFill, barHeight - 1, barHeight - 1, barHeight - 1);
	}

	    /* the bonus (shield etc.) as a thin blue line */
	if (otherPercent > 0) {
	    g2.setColor(new Color(79, 134, 198));
	    g2.fillRect(4, top + barHeight - 3, (int) (otherPercent * (canvasWidth - 8)), 2);
	}

	g2.setFont(ValueFont);
	fm = g2.getFontMetrics();
	cx = (canvasWidth - fm.stringWidth(theValue)) / 2;
	int cy = top + (barHeight + fm.getAscent() - fm.getDescent()) / 2;
	g2.setColor(new Color(0, 0, 0, 150));
	g2.drawString(theValue, cx + 1, cy + 1);
	g2.setColor(foregroundColor);
	g2.drawString(theValue, cx, cy);
    }

    public Dimension getPreferredSize() {
	return new Dimension(160, 22);
    }

    public void changeStats(String value, long maxValue, long secondValue) {

	percent = (float)Long.parseLong(value)/maxValue;
	theValue = value;

	if (secondValue == 0) {
	    otherPercent = 0;
	    otherMax = 0;
	}
	else if (secondValue > otherMax) {
	    otherMax = secondValue;
	    otherPercent = 1;
	}
	else {
	    otherPercent = (float)secondValue/otherMax;
	}

	repaint();
    }
}
