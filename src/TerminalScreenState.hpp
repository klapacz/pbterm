#if ! defined TERMINAL_SCREEN_STATE_HPP_
#define TERMINAL_SCREEN_STATE_HPP_

#include "TerminalScreen.hpp"

// Small display-side state holder for the Ghostty-backed fixed-cell screen.
//
// The important behavior is that a terminal screen snapshot remains the active
// redraw source until legacy text output explicitly replaces it. InkView's
// Repaint()/EVT_SHOW path can redraw asynchronously after Display::set_screen()
// has already painted a frame; if we do not keep this state, that later redraw
// falls back to the old Lines renderer and clears the terminal grid.
class TerminalScreenState
{
  public:

    bool
    has_screen( ) const
    {
        return m_has_screen;
    }


    TerminalScreen const &
    screen( ) const
    {
        return m_screen;
    }


    void
    set_screen( TerminalScreen const & screen )
    {
        m_screen = screen;
        m_has_screen = true;
    }


    void
    clear( )
    {
        m_screen = TerminalScreen( );
        m_has_screen = false;
    }

  private:

    bool m_has_screen = false;
    TerminalScreen m_screen;
};

#endif
